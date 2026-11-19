#include "core/shell.h"

#include <string.h>
#include <unistd.h>

extern char **environ;

/*
 * Session state. Deliberately small: fields appear when a milestone needs
 * them, not in anticipation of one.
 */

void wl_shell_init(struct wl_shell *sh, bool plain, bool interactive) {
    memset(sh, 0, sizeof *sh);
    sh->interactive = interactive;
    sh->plain = plain;
    sh->running = true;
    sh->last_status = 0;
    sh->tty_fd = interactive ? STDIN_FILENO : -1;
    sh->shell_pgid = getpgrp();
    wl_vars_init(&sh->vars, environ);
}

void wl_shell_destroy(struct wl_shell *sh) {
    wl_vars_free(&sh->vars);
}

void wl_shell_set_status(struct wl_shell *sh, int status) {
    sh->last_status = status;
}

void wl_shell_stop(struct wl_shell *sh, int status) {
    sh->last_status = status;
    sh->running = false;
}
