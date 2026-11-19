#include "exec/exec.h"

#include "ast/ast.h"
#include "builtins/builtins.h"
#include "core/buf.h"
#include "core/limits.h"
#include "core/shell.h"
#include "exec/path.h"
#include "jobs/signal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/*
 * Execution, M2.
 *
 * The parser understands the whole grammar; execution understands only what
 * can be run without expansion, redirection or process teardown. Anything else
 * is refused loudly with status 2, never ignored (docs/ROADMAP.md M2/M3).
 */

static void wl_terminal_give(int tty_fd, pid_t pgid) {
    if (tty_fd < 0) {
        return;
    }
    while (tcsetpgrp(tty_fd, pgid) != 0 && errno == EINTR) {
        /* retry: a signal can interrupt the ioctl */
    }
}

static int wl_status_from_wait(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

static int wl_wait_child(struct wl_shell *shell, pid_t child) {
    int status = 0;
    pid_t reaped;

    do {
        reaped = waitpid(child, &status, 0);
    } while (reaped < 0 && errno == EINTR);

    wl_terminal_give(shell->tty_fd, shell->shell_pgid);

    if (reaped < 0) {
        fprintf(stderr, "wonderland: wait: %s\n", strerror(errno));
        return 1;
    }
    return wl_status_from_wait(status);
}

int wl_exec_external(struct wl_shell *shell, char *const argv[]) {
    char path[WL_PATH_MAX];
    enum wl_path_status found;
    pid_t child;

    found = wl_path_find(argv[0], path, sizeof path);
    if (found == WL_PATH_NOT_FOUND) {
        fprintf(stderr, "wonderland: %s: command not found\n", argv[0]);
        return 127;
    }
    if (found == WL_PATH_NOT_EXECUTABLE) {
        fprintf(stderr, "wonderland: %s: Permission denied\n", argv[0]);
        return 126;
    }
    if (found == WL_PATH_ERROR) {
        fprintf(stderr, "wonderland: %s: %s\n", argv[0], strerror(errno));
        return 126;
    }

    child = fork();
    if (child < 0) {
        fprintf(stderr, "wonderland: fork: %s\n", strerror(errno));
        return 1;
    }

    if (child == 0) {
        wl_signal_reset_for_child();
        if (shell->tty_fd >= 0) {
            (void)setpgid(0, 0);
        }
        execve(path, argv, environ);
        fprintf(stderr, "wonderland: %s: %s\n", argv[0], strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);
    }

    /*
     * Both sides set the group to close the fork/exec race. Non-interactive
     * runs stay in the shell's group: there is no job control to hand the
     * terminal to, and a child in its own group would stop on its first tty
     * read with no way to resume it (docs/DECISIONS.md D-0051).
     */
    if (shell->tty_fd >= 0) {
        (void)setpgid(child, child);
        wl_terminal_give(shell->tty_fd, child);
    }

    return wl_wait_child(shell, child);
}

static bool wl_exec_refuse(struct wl_shell *shell, const char *what) {
    fprintf(stderr, "wonderland: %s is not implemented yet (M3)\n", what);
    wl_shell_set_status(shell, 2);
    return false;
}

static bool wl_exec_node(struct wl_shell *shell, const struct wl_node *node);

static bool wl_exec_simple(struct wl_shell *shell, const struct wl_node *node) {
    char *argv[WL_ARGV_MAX];
    struct wl_buf buffers[WL_ARGV_MAX];
    size_t count = node->u.simple.word_count;
    bool ok = true;

    if (node->redir_count > 0) {
        return wl_exec_refuse(shell, "redirection");
    }
    if (node->u.simple.assign_count > 0) {
        return wl_exec_refuse(shell, "variable assignment");
    }
    if (count == 0) {
        return wl_exec_refuse(shell, "a command with no words");
    }
    if (count + 1 > WL_ARGV_MAX) {
        fprintf(stderr, "wonderland: too many arguments (max %d)\n",
                WL_ARGV_MAX - 1);
        wl_shell_set_status(shell, 2);
        return false;
    }

    for (size_t index = 0; index < count; index++) {
        wl_buf_init(&buffers[index]);
        argv[index] = NULL;
    }
    argv[count] = NULL;

    for (size_t index = 0; index < count; index++) {
        if (!wl_word_literal(node->u.simple.words[index], &buffers[index])) {
            (void)wl_exec_refuse(shell, "expansion");
            ok = false;
            break;
        }
        argv[index] = wl_buf_take(&buffers[index]);
        if (argv[index] == NULL) {
            fprintf(stderr, "wonderland: out of memory\n");
            wl_shell_set_status(shell, 1);
            ok = false;
            break;
        }
    }

    if (ok) {
        if (!wl_builtin_run(shell, (int)count, argv)) {
            int status = wl_exec_external(shell, argv);

            wl_shell_set_status(shell, status);
        }
    }

    for (size_t index = 0; index < count; index++) {
        free(argv[index]);
    }
    return ok;
}

static bool wl_exec_pipeline(struct wl_shell *shell,
                             const struct wl_node *node) {
    if (node->u.pipeline.count != 1) {
        return wl_exec_refuse(shell, "a pipeline");
    }
    if (!wl_exec_node(shell, node->u.pipeline.commands[0])) {
        return false;
    }
    if (node->u.pipeline.bang) {
        wl_shell_set_status(shell,
                            shell->last_status == 0 ? 1 : 0);
    }
    return true;
}

static bool wl_exec_andor(struct wl_shell *shell, const struct wl_node *node) {
    if (!wl_exec_node(shell, node->u.andor.first)) {
        return false;
    }
    for (size_t index = 0; index < node->u.andor.count; index++) {
        const struct wl_andor_link *link = &node->u.andor.links[index];
        bool run = (link->op == WL_ANDOR_AND)
                       ? (shell->last_status == 0)
                       : (shell->last_status != 0);

        if (!run) {
            continue;
        }
        if (!wl_exec_node(shell, link->node)) {
            return false;
        }
    }
    return true;
}

static bool wl_exec_list(struct wl_shell *shell, const struct wl_node *node) {
    for (size_t index = 0; index < node->u.list.count; index++) {
        if (!shell->running) {
            break;
        }
        if (node->u.list.async[index]) {
            return wl_exec_refuse(shell, "a background job");
        }
        if (!wl_exec_node(shell, node->u.list.statements[index])) {
            return false;
        }
    }
    return true;
}

static bool wl_exec_node(struct wl_shell *shell, const struct wl_node *node) {
    switch (node->kind) {
    case WL_NODE_LIST:
        return wl_exec_list(shell, node);
    case WL_NODE_ANDOR:
        return wl_exec_andor(shell, node);
    case WL_NODE_PIPELINE:
        return wl_exec_pipeline(shell, node);
    case WL_NODE_SIMPLE:
        return wl_exec_simple(shell, node);
    default:
        return wl_exec_refuse(shell, "a compound command");
    }
}

bool wl_exec_program(struct wl_shell *shell, const struct wl_node *program) {
    if (program == NULL) {
        return true;
    }
    return wl_exec_node(shell, program);
}
