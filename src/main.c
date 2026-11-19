#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ast/arena.h"
#include "ast/ast.h"
#include "core/buf.h"
#include "core/diag.h"
#include "core/exit_code.h"
#include "core/limits.h"
#include "core/opts.h"
#include "core/shell.h"
#include "core/version.h"
#include "exec/exec.h"
#include "jobs/signal.h"
#include "line/read.h"
#include "parse/parse.h"
#include "term/prompt.h"
#include "term/term.h"

/*
 * Entry point and read/parse/execute loop.
 *
 * Three input paths: -c STRING (one complete unit), stdin (read whole, one
 * unit), interactive (one logical line at a time, with a continuation prompt
 * while the parser reports `incomplete`). All parsing goes through
 * wl_parse_source; execution goes through wl_exec_program.
 */

static bool wl_run_source(struct wl_shell *shell, struct wl_arena *arena,
                          const char *source, size_t length,
                          const char *name) {
    struct wl_diag diag;
    struct wl_node *program;

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
    return wl_exec_program(shell, program);
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

static void wl_run_interactive(struct wl_shell *shell, struct wl_arena *arena) {
    char line[WL_LINE_MAX];
    size_t used = 0;
    bool continuing = false;

    for (;;) {
        char prompt[256];
        char cwd[WL_PATH_MAX];
        enum wl_line_status read_status;
        size_t length = 0;

        if (getcwd(cwd, sizeof cwd) == NULL) {
            (void)snprintf(cwd, sizeof cwd, "?");
        }
        if (continuing) {
            fputs("> ", stderr);
        } else {
            (void)wl_prompt_format(prompt, sizeof prompt, shell->last_status,
                                   cwd, getenv("HOME"));
            fputs(prompt, stderr);
        }
        fflush(stderr);

        if (used >= sizeof line) {
            fprintf(stderr, "wonderland: line too long (max %d bytes)\n",
                    WL_LINE_MAX - 1);
            wl_shell_set_status(shell, 2);
            used = 0;
            continuing = false;
            wl_arena_reset(arena);
            continue;
        }

        read_status = wl_line_read(shell->tty_fd, line + used,
                                   sizeof line - used, &length);
        if (read_status == WL_LINE_INTERRUPTED) {
            fputc('\n', stderr);
            fflush(stderr);
            used = 0;
            continuing = false;
            wl_arena_reset(arena);
            continue;
        }
        if (read_status == WL_LINE_EOF) {
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
        if (read_status == WL_LINE_TOO_LONG) {
            fprintf(stderr, "wonderland: line too long (max %d bytes)\n",
                    WL_LINE_MAX - 1);
            wl_shell_set_status(shell, 2);
            used = 0;
            continuing = false;
            wl_arena_reset(arena);
            continue;
        }
        if (read_status == WL_LINE_ERROR) {
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

            (void)wl_exec_program(shell, program);
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

static void wl_run_stream(struct wl_shell *shell, struct wl_arena *arena,
                          FILE *input) {
    struct wl_buf source;

    wl_buf_init(&source);
    if (wl_read_all(input, &source)) {
        (void)wl_run_source(shell, arena,
                            (source.data != NULL) ? source.data : "",
                            source.len, "<stdin>");
        wl_arena_reset(arena);
    } else {
        wl_shell_set_status(shell, 2);
    }
    wl_buf_free(&source);
}

int main(int argc, char **argv) {
    struct wl_options opts;
    enum wl_opt_action action = wl_opts_parse(argc, argv, &opts);
    struct wl_shell shell;
    struct wl_term term;
    struct wl_arena arena;
    bool interactive;

    if (action == WL_OPT_USAGE_ERROR) {
        fprintf(stderr, "wonderland: %s\n", opts.usage_error);
        fprintf(stderr, "usage: wonderland [--plain] [--version] [-c STRING]"
                        " [SCRIPT | -]\n");
        return WL_EXIT_USAGE;
    }

    /* Plain mode is resolved before anything could observe the world. */
    wl_opts_apply_env(&opts, getenv("WONDERLAND_PLAIN"));

    if (action == WL_OPT_VERSION) {
        printf("%s\n", WL_VERSION_LINE);
        return WL_EXIT_OK;
    }

    if (action == WL_OPT_SCRIPT) {
        fprintf(stderr,
                "wonderland: script files are not supported yet (M3); use -c "
                "or stdin\n");
        return WL_EXIT_UNIMPLEMENTED;
    }

    interactive = (action == WL_OPT_START) && !opts.stdin_stream &&
                  isatty(STDIN_FILENO) && isatty(STDERR_FILENO);

    wl_shell_init(&shell, opts.plain, interactive);
    wl_arena_init(&arena);

    term.fd = -1;
    term.saved = false;
    if (interactive) {
        (void)wl_term_save(&term, STDIN_FILENO);
        wl_signal_init_interactive();
    }

    if (action == WL_OPT_COMMAND) {
        if (strlen(opts.command) > WL_SOURCE_MAX) {
            fprintf(stderr, "wonderland: -c input is too large (max %d bytes)\n",
                    WL_SOURCE_MAX);
            return WL_EXIT_USAGE;
        }
        (void)wl_run_source(&shell, &arena, opts.command,
                            strlen(opts.command), "-c");
        wl_arena_reset(&arena);
    } else if (interactive) {
        wl_run_interactive(&shell, &arena);
    } else {
        wl_run_stream(&shell, &arena, stdin);
    }

    wl_arena_reset(&arena);
    if (interactive) {
        wl_term_restore(&term);
    }
    wl_shell_destroy(&shell);
    return shell.last_status;
}
