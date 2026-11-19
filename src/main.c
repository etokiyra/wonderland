#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ast/arena.h"
#include "ast/ast.h"
#include "core/buf.h"
#include "core/diag.h"
#include "core/exit_code.h"
#include "core/limits.h"
#include "core/opts.h"
#include "core/shell.h"
#include "core/state.h"
#include "content/pack.h"
#include "core/version.h"
#include "exec/exec.h"
#include "jobs/job.h"
#include "jobs/signal.h"
#include "line/editor.h"
#include "line/history.h"
#include "line/read.h"
#include "narrative/persona.h"
#include "parse/parse.h"
#include "term/prompt.h"
#include "term/term.h"

/*
 * Entry point and read/parse/execute loop.
 *
 * Three input paths: -c STRING (one complete unit), stdin (read whole, one
 * unit), interactive (one logical line at a time, with a continuation prompt
 * while the parser reports `incomplete`) and a script file. The source text is
 * published on the shell for the duration of execution so that function
 * definitions can copy their own body into a session-lifetime arena.
 */

/*
 * The narrative hook.
 *
 * Runs only for an interactive, non-plain session. The event is immutable
 * data; the persona returns borrowed text; the shell writes it to stderr and
 * flushes before the prompt. stdout is never touched.
 */
static void wl_emit_narrative(struct wl_shell *shell, enum wl_event_kind kind,
                              int status, const char *cmdline,
                              const char *cwd) {
    struct wl_event event;
    struct wl_line output;
    char sanitized[200];
    size_t at = 0;

    if (!shell->interactive || shell->plain) {
        return;
    }
    if (kind != WL_EV_EXIT && !shell->running) {
        return;
    }
    if (cmdline != NULL) {
        while (cmdline[at] != '\0' && at + 1 < sizeof sanitized && at < 120) {
            unsigned char c = (unsigned char)cmdline[at];

            sanitized[at] = (c >= 32 && c < 127) ? (char)c : ' ';
            at++;
        }
    }
    sanitized[at] = '\0';

    memset(&event, 0, sizeof event);
    event.kind = kind;
    event.status = status;
    event.now = time(NULL);
    event.cycle = shell->library.cycle;
    event.cmdline = sanitized;
    event.argv0 = sanitized;
    event.cwd = cwd;

    if (wl_narrative_react(&shell->narrative, &event, &output) &&
        output.len > 0) {
        char rendered[1024];
        size_t length = wl_narrative_render(
            output.text, &event, wl_narrative_persona(&shell->narrative),
            rendered, sizeof rendered);

        fwrite(rendered, 1, length, stderr);
        fputc('\n', stderr);
        fflush(stderr);
    }
}

/*
 * Observational job hooks (Black Rabbit). The job table only keeps counters
 * and the last finished command; emitting stays here so jobs never depends on
 * the narrative library.
 */
static void wl_emit_job_starts(struct wl_shell *shell, unsigned long *seen,
                               const char *cwd) {
    while (*seen < shell->jobs.started) {
        struct wl_job *job = shell->jobs.head;

        if (job != NULL && job->background) {
            wl_emit_narrative(shell, WL_EV_JOB_START, 0, job->command, cwd);
        }
        (*seen)++;
    }
}

static void wl_emit_job_finishes(struct wl_shell *shell, unsigned long *seen,
                                 const char *cwd) {
    while (*seen < shell->jobs.finished) {
        wl_emit_narrative(shell, WL_EV_JOB_FINISH, 0,
                          shell->jobs.last_finished_command, cwd);
        (*seen)++;
    }
}

static bool wl_env_flag(const char *name) {
    const char *value = getenv(name);

    return value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
}

/*
 * Death screen: owned presentation on a non-zero status. Opt-in
 * (WONDERLAND_DEATH_SCREEN=1), interactive non-plain only, and it never
 * touches `last_status`.
 */
static void wl_emit_death_screen(const struct wl_shell *shell) {
    if (!shell->interactive || shell->plain || !shell->running) {
        return;
    }
    if (!wl_env_flag("WONDERLAND_DEATH_SCREEN")) {
        return;
    }
    fputs("\n  [ the cycle continues without you ]\n", stderr);
    fflush(stderr);
}

/*
 * Mirror mode may alter only Wonderland-owned presentation. It currently
 * adds a prompt fragment; it never touches command output, but the check is
 * still kept here rather than in command execution.
 */
static void wl_apply_mirror(char *prompt, size_t capacity) {
    size_t used;

    if (!wl_env_flag("WONDERLAND_MIRROR")) {
        return;
    }
    used = strlen(prompt);
    if (used + strlen(" [mirror]") + 1 <= capacity) {
        (void)snprintf(prompt + used, capacity - used, " [mirror]");
    }
}

/*
 * Alice's Library is opened only for interactive, non-plain sessions: plain
 * mode must not read or write narrative state at all. A missing file is a
 * fresh cycle; a corrupt file is a fresh cycle plus exactly one note.
 */
static void wl_library_open(struct wl_shell *shell) {
    bool corrupt = false;
    const struct wl_persona *persona;

    if (!shell->interactive || shell->plain) {
        return;
    }
    shell->library_path = wl_state_default_path();
    if (shell->library_path == NULL) {
        return;
    }
    shell->library_enabled = true;

    if (wl_state_load(shell->library_path, &shell->library, &corrupt)) {
        if (shell->library.persona != NULL) {
            (void)wl_narrative_possess(&shell->narrative,
                                       shell->library.persona);
        }
    } else if (corrupt) {
        fprintf(stderr,
                "wonderland: Alice's Library state was unreadable; starting "
                "a new cycle\n");
        wl_state_free(&shell->library);
        wl_state_init(&shell->library);
    }
    shell->library.cycle++;
    persona = wl_narrative_persona(&shell->narrative);
    if (persona != NULL) {
        (void)wl_state_set_persona(&shell->library, persona->id);
    }
    if (!wl_state_save(shell->library_path, &shell->library)) {
        fprintf(stderr, "wonderland: could not write Alice's Library state\n");
    }
}

static void wl_library_close(struct wl_shell *shell) {
    const struct wl_persona *persona;

    if (!shell->library_enabled || shell->library_path == NULL) {
        return;
    }
    persona = wl_narrative_persona(&shell->narrative);
    if (persona != NULL) {
        (void)wl_state_set_persona(&shell->library, persona->id);
    }
    (void)wl_state_save(shell->library_path, &shell->library);
}

/*
 * Pack search order: WONDERLAND_PACKS (colon separated), the XDG data
 * directory (where `make install` puts core packs), and the development tree.
 * Missing paths are skipped by the loader.
 */
static struct wl_packset *wl_load_packs(void) {
    const char *env = getenv("WONDERLAND_PACKS");
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    const char *paths[12];
    char storage[8][512];
    size_t count = 0;
    size_t stored = 0;
    char *env_copy = NULL;

    if (env != NULL && env[0] != '\0') {
        char *cursor;
        char *save = NULL;

        env_copy = strdup(env);
        cursor = (env_copy != NULL) ? strtok_r(env_copy, ":", &save) : NULL;
        while (cursor != NULL && count < 6) {
            paths[count++] = cursor;
            cursor = strtok_r(NULL, ":", &save);
        }
    }

    /*
     * Prefer the development tree when it is present; otherwise use the
     * installed XDG location. Adding both would load the same persona twice
     * and (correctly) trip duplicate-id validation.
     */
    {
        struct stat info;

        if (stat("packs/core", &info) != 0 || !S_ISDIR(info.st_mode)) {
            if (xdg != NULL && xdg[0] != '\0') {
                (void)snprintf(storage[stored], sizeof storage[stored],
                               "%s/wonderland/packs/core", xdg);
                paths[count++] = storage[stored++];
                (void)snprintf(storage[stored], sizeof storage[stored],
                               "%s/wonderland/packs", xdg);
                paths[count++] = storage[stored++];
            } else if (home != NULL && home[0] != '\0') {
                (void)snprintf(storage[stored], sizeof storage[stored],
                               "%s/.local/share/wonderland/packs/core", home);
                paths[count++] = storage[stored++];
                (void)snprintf(storage[stored], sizeof storage[stored],
                               "%s/.local/share/wonderland/packs", home);
                paths[count++] = storage[stored++];
            }
        }
    }

    if (count < sizeof paths / sizeof paths[0]) {
        (void)snprintf(storage[stored], sizeof storage[stored],
                       "%s", "packs/core");
        paths[count++] = storage[stored++];
    }
    if (count < sizeof paths / sizeof paths[0]) {
        (void)snprintf(storage[stored], sizeof storage[stored], "%s", "packs");
        paths[count++] = storage[stored++];
    }

    {
        struct wl_packset *packs = wl_packset_load(paths, count);

        free(env_copy);
        return packs;
    }
}

static bool wl_run_source(struct wl_shell *shell, struct wl_arena *arena,
                          const char *source, size_t length,
                          const char *name) {
    struct wl_diag diag;
    struct wl_node *program;
    bool ok;

    wl_diag_clear(&diag);
    program = wl_parse_source(source, length, name, arena, &diag);
    if (program == NULL) {
        if (diag.set) {
            wl_diag_print(&diag, source, length, name, stderr);
        } else {
            fprintf(stderr,
                    "wonderland: internal error: parse failed without a "
                    "diagnostic\n");
        }
        wl_shell_set_status(shell, 2);
        return false;
    }

    shell->source = source;
    shell->source_length = length;
    ok = wl_exec_program(shell, program);
    shell->source = NULL;
    shell->source_length = 0;
    return ok;
}

/*
 * A trailing backslash escapes the newline, so the shell must read the next
 * line before tokenizing. The parser cannot see this: by the time it has
 * removed the escape, the command looks complete.
 */
static bool wl_line_continues(const char *text, size_t length) {
    size_t backslashes = 0;
    size_t at = length;

    if (at == 0 || text[at - 1] != '\n') {
        return false;
    }
    at--;
    while (at > 0 && text[at - 1] == '\\') {
        backslashes++;
        at--;
    }
    return (backslashes % 2) == 1;
}

static void wl_run_interactive(struct wl_shell *shell, struct wl_arena *arena,
                               struct wl_editor *editor) {
    char line[WL_LINE_MAX];
    char previous_cwd[WL_PATH_MAX];
    bool have_previous_cwd = false;
    size_t used = 0;
    bool continuing = false;
    unsigned long seen_started = shell->jobs.started;
    unsigned long seen_finished = shell->jobs.finished;

    if (getcwd(previous_cwd, sizeof previous_cwd) != NULL) {
        have_previous_cwd = true;
    }

    for (;;) {
        char prompt[256];
        char cwd[WL_PATH_MAX];
        enum wl_edit_status read_status;
        size_t length = 0;

        if (getcwd(cwd, sizeof cwd) == NULL) {
            (void)snprintf(cwd, sizeof cwd, "?");
        }
        if (continuing) {
            (void)snprintf(prompt, sizeof prompt, "%s", "> ");
        } else {
            const char *custom = getenv("WONDERLAND_PROMPT");

            wl_job_notify(shell);
            wl_emit_job_finishes(shell, &seen_finished, cwd);
            if (custom != NULL && custom[0] != '\0') {
                (void)snprintf(prompt, sizeof prompt, "%s", custom);
            } else {
                (void)wl_prompt_format(prompt, sizeof prompt,
                                       shell->last_status, cwd,
                                       getenv("HOME"));
            }
            wl_apply_mirror(prompt, sizeof prompt);
        }

        if (used >= sizeof line) {
            fprintf(stderr, "wonderland: line too long (max %d bytes)\n",
                    WL_LINE_MAX - 1);
            wl_shell_set_status(shell, 2);
            used = 0;
            continuing = false;
            wl_arena_reset(arena);
            continue;
        }

        read_status = wl_editor_read(editor, prompt, line + used,
                                     sizeof line - used, &length);
        if (read_status == WL_EDIT_INTERRUPTED) {
            wl_exec_trap_dispatch(shell);
            used = 0;
            continuing = false;
            wl_arena_reset(arena);
            continue;
        }
        if (read_status == WL_EDIT_EOF) {
            fputc('\n', stderr);
            fflush(stderr);
            if (used > 0) {
                fprintf(stderr,
                        "wonderland: <stdin>: syntax error: unexpected end of "
                        "file\n");
                wl_shell_set_status(shell, 2);
            }
            break;
        }
        if (read_status == WL_EDIT_TOO_LONG) {
            fprintf(stderr, "wonderland: line too long (max %d bytes)\n",
                    WL_LINE_MAX - 1);
            wl_shell_set_status(shell, 2);
            used = 0;
            continuing = false;
            wl_arena_reset(arena);
            continue;
        }
        if (read_status == WL_EDIT_ERROR) {
            fprintf(stderr, "wonderland: read: %s\n", strerror(errno));
            wl_shell_set_status(shell, 1);
            break;
        }

        /* Keep the newline: here-document bodies and line structure need it. */
        if (used + length + 2 > sizeof line) {
            fprintf(stderr, "wonderland: line too long (max %d bytes)\n",
                    WL_LINE_MAX - 1);
            wl_shell_set_status(shell, 2);
            used = 0;
            continuing = false;
            wl_arena_reset(arena);
            continue;
        }
        line[used + length] = '\n';
        used += length + 1;
        line[used] = '\0';

        if (wl_line_continues(line, used)) {
            continuing = true;
            continue;
        }

        {
            struct wl_diag diag;
            struct wl_node *program;

            wl_diag_clear(&diag);
            program = wl_parse_source(line, used, "<stdin>", arena, &diag);
            if (program == NULL) {
                if (diag.incomplete) {
                    wl_arena_reset(arena);
                    continuing = true;
                    continue;
                }
                wl_diag_print(&diag, line, used, "<stdin>", stderr);
                wl_shell_set_status(shell, 2);
                used = 0;
                continuing = false;
                wl_arena_reset(arena);
                continue;
            }

            shell->source = line;
            shell->source_length = used;
            (void)wl_exec_program(shell, program);
            shell->source = NULL;
            shell->source_length = 0;
            wl_emit_job_starts(shell, &seen_started, cwd);
            wl_emit_job_finishes(shell, &seen_finished, cwd);
            wl_emit_narrative(shell, WL_EV_POST_EXEC, shell->last_status, line,
                              cwd);
            wl_emit_death_screen(shell);
            {
                char after[WL_PATH_MAX];

                if (getcwd(after, sizeof after) != NULL) {
                    if (have_previous_cwd &&
                        strcmp(after, previous_cwd) != 0) {
                        wl_emit_narrative(shell, WL_EV_DIR_CHANGE,
                                          shell->last_status, line, after);
                    }
                    (void)snprintf(previous_cwd, sizeof previous_cwd, "%s",
                                   after);
                    have_previous_cwd = true;
                }
            }
            wl_arena_reset(arena);
            used = 0;
            continuing = false;
            if (!shell->running) {
                break;
            }
        }
    }
}

static bool wl_read_all(FILE *input, struct wl_buf *source) {
    char chunk[4096];
    size_t count;

    while ((count = fread(chunk, 1, sizeof chunk, input)) > 0) {
        if (source->len + count > WL_SOURCE_MAX) {
            fprintf(stderr, "wonderland: input is too large (max %d bytes)\n",
                    WL_SOURCE_MAX);
            return false;
        }
        if (!wl_buf_append(source, chunk, count)) {
            fprintf(stderr, "wonderland: out of memory\n");
            return false;
        }
    }
    if (ferror(input)) {
        fprintf(stderr, "wonderland: read: %s\n", strerror(errno));
        return false;
    }
    return true;
}

static bool wl_read_file(const char *path, struct wl_buf *source) {
    FILE *file = fopen(path, "r");
    bool ok;

    if (file == NULL) {
        fprintf(stderr, "wonderland: %s: %s\n", path, strerror(errno));
        return false;
    }
    ok = wl_read_all(file, source);
    (void)fclose(file);
    return ok;
}

static void wl_run_stream(struct wl_shell *shell, struct wl_arena *arena,
                          FILE *input, const char *name) {
    struct wl_buf source;

    wl_buf_init(&source);
    if (wl_read_all(input, &source)) {
        (void)wl_run_source(shell, arena,
                            (source.data != NULL) ? source.data : "",
                            source.len, name);
        wl_arena_reset(arena);
    } else {
        wl_shell_set_status(shell, 2);
    }
    wl_buf_free(&source);
}

static void wl_set_positional(struct wl_shell *shell,
                              enum wl_opt_action action,
                              const struct wl_options *opts, char **argv) {
    const char *name0 = "wonderland";
    char **arguments = NULL;
    size_t count = 0;

    if (action == WL_OPT_COMMAND) {
        if (opts->arg_count > 0) {
            name0 = argv[opts->arg_start];
            arguments = argv + opts->arg_start + 1;
            count = opts->arg_count - 1;
        }
    } else if (action == WL_OPT_SCRIPT) {
        name0 = opts->script;
        arguments = argv + opts->arg_start;
        count = opts->arg_count;
    } else if (opts->stdin_stream && opts->arg_count > 0) {
        name0 = argv[opts->arg_start];
        arguments = argv + opts->arg_start + 1;
        count = opts->arg_count - 1;
    }

    if (!wl_shell_set_args(shell, name0, arguments, count)) {
        fprintf(stderr, "wonderland: out of memory\n");
    }
}

int main(int argc, char **argv) {
    struct wl_options opts;
    enum wl_opt_action action = wl_opts_parse(argc, argv, &opts);
    struct wl_shell shell;
    struct wl_term term;
    struct wl_arena arena;
    struct wl_editor editor;
    struct wl_packset *packs = NULL;
    struct wl_history *history = NULL;
    char *history_path = NULL;
    bool have_editor = false;
    bool interactive;

    if (action == WL_OPT_USAGE_ERROR) {
        fprintf(stderr, "wonderland: %s\n", opts.usage_error);
        fprintf(stderr, "usage: wonderland [--plain] [--version] [-c STRING]"
                        " [SCRIPT | -] [ARG...]\n");
        return WL_EXIT_USAGE;
    }

    /* Plain mode is resolved before anything could observe the world. */
    wl_opts_apply_env(&opts, getenv("WONDERLAND_PLAIN"));

    if (action == WL_OPT_VERSION) {
        printf("%s\n", WL_VERSION_LINE);
        return WL_EXIT_OK;
    }

    interactive = (action == WL_OPT_START) && !opts.stdin_stream &&
                  isatty(STDIN_FILENO) && isatty(STDERR_FILENO);

    wl_shell_init(&shell, opts.plain, interactive);
    wl_narrative_state_init(&shell.narrative);
    wl_arena_init(&arena);
    wl_set_positional(&shell, action, &opts, argv);

    term.fd = -1;
    term.saved = false;
    if (interactive) {
        (void)wl_term_save(&term, STDIN_FILENO);
        wl_signal_init_interactive();
        wl_job_init(&shell);

        history = wl_history_new();
        history_path = wl_history_default_path();
        if (history != NULL && history_path != NULL) {
            (void)wl_history_load(history, history_path);
        }
        if (!wl_editor_init(&editor, STDIN_FILENO, STDERR_FILENO, history)) {
            fprintf(stderr, "wonderland: out of memory\n");
            wl_term_restore(&term);
            wl_exec_shutdown(&shell);
            wl_shell_destroy(&shell);
            return 1;
        }
        have_editor = true;

        if (!shell.plain) {
            char cwd[WL_PATH_MAX];

            packs = wl_load_packs();
            wl_narrative_attach(&shell.narrative, packs);
            wl_library_open(&shell);
            if (getcwd(cwd, sizeof cwd) != NULL) {
                wl_emit_narrative(&shell, WL_EV_SESSION_START, 0, NULL, cwd);
            }
        } else {
            wl_narrative_attach(&shell.narrative, NULL);
        }
    }

    if (action == WL_OPT_COMMAND) {
        size_t length = strlen(opts.command);

        if (length > WL_SOURCE_MAX) {
            fprintf(stderr, "wonderland: -c input is too large (max %d bytes)\n",
                    WL_SOURCE_MAX);
            wl_shell_set_status(&shell, 2);
        } else {
            (void)wl_run_source(&shell, &arena, opts.command, length, "-c");
        }
        wl_arena_reset(&arena);
    } else if (action == WL_OPT_SCRIPT) {
        struct wl_buf source;

        wl_buf_init(&source);
        if (wl_read_file(opts.script, &source)) {
            (void)wl_run_source(&shell, &arena,
                                (source.data != NULL) ? source.data : "",
                                source.len, opts.script);
        } else {
            wl_shell_set_status(&shell, 127);
        }
        wl_buf_free(&source);
        wl_arena_reset(&arena);
    } else if (interactive) {
        wl_run_interactive(&shell, &arena, &editor);
    } else {
        wl_run_stream(&shell, &arena, stdin, "<stdin>");
    }

    wl_exec_run_exit_trap(&shell);
    if (interactive && !shell.plain) {
        char cwd[WL_PATH_MAX];

        if (getcwd(cwd, sizeof cwd) != NULL) {
            wl_emit_narrative(&shell, WL_EV_EXIT, shell.last_status, NULL, cwd);
        }
        wl_library_close(&shell);
    }
    wl_arena_reset(&arena);
    if (have_editor) {
        wl_editor_free(&editor);
    }
    if (history != NULL) {
        if (history_path != NULL) {
            (void)wl_history_save(history, history_path);
        }
        wl_history_free(history);
    }
    free(history_path);
    if (interactive) {
        wl_term_restore(&term);
    }
    {
        int status = shell.last_status;

        wl_exec_shutdown(&shell);
        wl_shell_destroy(&shell);
        wl_packset_free(packs);
        return status;
    }
}
