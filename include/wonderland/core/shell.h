#ifndef WL_CORE_SHELL_H
#define WL_CORE_SHELL_H

#include <stdbool.h>
#include <sys/types.h>

#include "core/var.h"

/*
 * Session state.
 *
 * One instance, owned by main() and passed explicitly. Fields are added as
 * milestones need them; there is no hidden global state. `last_status` has a
 * single writer contract: only the execution path (and the builtins, through
 * wl_shell_set_status) update it.
 */

struct wl_shell {
    /* stdin and stderr are terminals and no command string was given. */
    bool interactive;

    /* Plain mode: narrative disabled. Unused until M3.5 by design. */
    bool plain;

    /* False once the `exit` builtin has run; the loop must stop. */
    bool running;

    /* Exit status of the most recent command. */
    int last_status;

    /* Process group the shell owns the terminal with. */
    pid_t shell_pgid;

    /* Terminal fd for foreground handoff, or -1 when not interactive. */
    int tty_fd;

    /* Shell variables and the exported environment. */
    struct wl_vars vars;
};

void wl_shell_init(struct wl_shell *sh, bool plain, bool interactive);

/* Releases the variable store. The shell struct itself is stack-owned. */
void wl_shell_destroy(struct wl_shell *sh);

/* Records the status of the last command. */
void wl_shell_set_status(struct wl_shell *sh, int status);

/* Records a status and requests that the loop stop (used by `exit`). */
void wl_shell_stop(struct wl_shell *sh, int status);

#endif /* WL_CORE_SHELL_H */
