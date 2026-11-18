#include "exec/exec.h"

#include "builtins/builtins.h"
#include "core/limits.h"
#include "core/shell.h"
#include "exec/m1_split.h"
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
 * Execution, M1: one command line, no pipelines, no redirections, no
 * background jobs. What is here is the part that must be right from the
 * beginning: PATH resolution, 126/127 semantics, process-group handoff, and
 * status propagation (docs/ROADMAP.md M1).
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

static int wl_wait_child(struct wl_shell *sh, pid_t child) {
    int status = 0;
    pid_t reaped;

    do {
        reaped = waitpid(child, &status, 0);
    } while (reaped < 0 && errno == EINTR);

    wl_terminal_give(sh->tty_fd, sh->shell_pgid);

    if (reaped < 0) {
        fprintf(stderr, "wonderland: wait: %s\n", strerror(errno));
        return 1;
    }
    return wl_status_from_wait(status);
}

int wl_exec_external(struct wl_shell *sh, char *const argv[]) {
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
        /* Child: fresh dispositions, own process group, then become the target. */
        wl_signal_reset_for_child();
        if (sh->tty_fd >= 0) {
            (void)setpgid(0, 0);
        }
        execve(path, argv, environ);
        fprintf(stderr, "wonderland: %s: %s\n", argv[0], strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);
    }

    /*
     * Both sides set the group to close the fork/exec race; whichever runs
     * second gets EACCES/EPERM and that is fine. Non-interactive runs stay in
     * the shell's process group: there is no job control to hand the terminal
     * to, and a child in its own group would stop on the first tty read
     * (SIGTTIN) with no way back.
     */
    if (sh->tty_fd >= 0) {
        (void)setpgid(child, child);
        wl_terminal_give(sh->tty_fd, child);
    }

    return wl_wait_child(sh, child);
}

bool wl_exec_line(struct wl_shell *sh, const char *line) {
    char buffer[WL_LINE_MAX];
    char *argv[WL_ARGV_MAX];
    size_t length;
    size_t argc;
    int status;

    if (line == NULL) {
        return false;
    }
    length = strlen(line);
    if (length >= sizeof buffer) {
        fprintf(stderr, "wonderland: command line too long (max %d bytes)\n",
                WL_LINE_MAX - 1);
        wl_shell_set_status(sh, 2);
        return true;
    }

    memcpy(buffer, line, length + 1);
    argc = wl_m1_split(buffer, argv, WL_ARGV_MAX);
    if (argc == 0) {
        return false;
    }
    if (argc >= WL_ARGV_MAX) {
        fprintf(stderr, "wonderland: too many arguments (max %d)\n",
                WL_ARGV_MAX - 1);
        wl_shell_set_status(sh, 2);
        return true;
    }

    if (wl_builtin_run(sh, (int)argc, argv)) {
        return true; /* the builtin recorded its own status */
    }

    status = wl_exec_external(sh, argv);
    wl_shell_set_status(sh, status);
    return true;
}
