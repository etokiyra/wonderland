#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/exit_code.h"
#include "core/limits.h"
#include "core/opts.h"
#include "core/shell.h"
#include "core/version.h"
#include "exec/exec.h"
#include "jobs/signal.h"
#include "line/read.h"
#include "term/prompt.h"
#include "term/term.h"

/*
 * Milestone 1 entry point: option handling, the read/execute loop, and the
 * terminal handoff around it. The loop lives here because main() is the only
 * component allowed to depend on everything (docs/ARCHITECTURE.md §3).
 *
 * Deliberately absent at this milestone: pipelines, redirections, quoting,
 * expansion, job control, history, the line editor, and the narrative layer.
 */

static void wl_strip_eol(char *line) {
    size_t length = strlen(line);

    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[--length] = '\0';
    }
}

/*
 * -c STRING: every newline-separated line is executed in order, stopping when
 * the shell stops. There is no `;` handling yet; that is the parser's job and
 * the parser does not exist (docs/ROADMAP.md M2).
 */
static void wl_run_string(struct wl_shell *sh, const char *command) {
    char buffer[WL_LINE_MAX];
    const char *cursor = command;

    while (*cursor != '\0' && sh->running) {
        const char *newline = strchr(cursor, '\n');
        size_t length = (newline != NULL) ? (size_t)(newline - cursor)
                                          : strlen(cursor);

        if (length >= sizeof buffer) {
            fprintf(stderr, "wonderland: command line too long (max %d bytes)\n",
                    WL_LINE_MAX - 1);
            wl_shell_set_status(sh, 2);
        } else {
            memcpy(buffer, cursor, length);
            buffer[length] = '\0';
            wl_strip_eol(buffer);
            if (buffer[0] != '\0') {
                (void)wl_exec_line(sh, buffer);
            }
        }

        if (newline == NULL) {
            break;
        }
        cursor = newline + 1;
    }
}

static void wl_run_interactive(struct wl_shell *sh) {
    char line[WL_LINE_MAX];

    for (;;) {
        char prompt[256];
        char cwd[WL_PATH_MAX];
        size_t length = 0;
        enum wl_line_status read_status;

        if (getcwd(cwd, sizeof cwd) == NULL) {
            (void)snprintf(cwd, sizeof cwd, "?");
        }
        (void)wl_prompt_format(prompt, sizeof prompt, sh->last_status, cwd,
                               getenv("HOME"));
        fputs(prompt, stderr);
        fflush(stderr);

        read_status = wl_line_read(sh->tty_fd, line, sizeof line, &length);
        if (read_status == WL_LINE_INTERRUPTED) {
            fputc('\n', stderr);
            fflush(stderr);
            continue;
        }
        if (read_status == WL_LINE_EOF) {
            fputc('\n', stderr);
            fflush(stderr);
            break;
        }
        if (read_status == WL_LINE_TOO_LONG) {
            fprintf(stderr, "wonderland: line too long (max %d bytes)\n",
                    WL_LINE_MAX - 1);
            wl_shell_set_status(sh, 2);
            continue;
        }
        if (read_status == WL_LINE_ERROR) {
            fprintf(stderr, "wonderland: read: %s\n", strerror(errno));
            wl_shell_set_status(sh, 1);
            break;
        }

        wl_strip_eol(line);
        if (line[0] != '\0') {
            (void)wl_exec_line(sh, line);
        }
        if (!sh->running) {
            break;
        }
    }
}

/* Non-interactive stdin: a pipe, a here-doc, or `-` on a terminal. */
static void wl_run_stream(struct wl_shell *sh, FILE *input) {
    char *line = NULL;
    size_t capacity = 0;
    ssize_t count;

    while ((count = getline(&line, &capacity, input)) != -1 && sh->running) {
        (void)count;
        wl_strip_eol(line);
        if (line[0] != '\0') {
            (void)wl_exec_line(sh, line);
        }
    }
    free(line);
}

int main(int argc, char **argv) {
    struct wl_options opts;
    enum wl_opt_action action = wl_opts_parse(argc, argv, &opts);
    struct wl_shell shell;
    struct wl_term term;
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

    term.fd = -1;
    term.saved = false;
    if (interactive) {
        (void)wl_term_save(&term, STDIN_FILENO);
        wl_signal_init_interactive();
    }

    if (action == WL_OPT_COMMAND) {
        wl_run_string(&shell, opts.command);
    } else if (interactive) {
        wl_run_interactive(&shell);
    } else {
        wl_run_stream(&shell, stdin);
    }

    if (interactive) {
        wl_term_restore(&term);
    }
    return shell.last_status;
}
