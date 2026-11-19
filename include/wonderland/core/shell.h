#ifndef WL_CORE_SHELL_H
#define WL_CORE_SHELL_H

#include <stdbool.h>
#include <stddef.h>
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

/* One positional-parameter scope: the base shell/script, or a function call. */
struct wl_frame {
    char **args; /* $1..$n, owned strings */
    size_t count;
    char *name0; /* $0, owned */
    struct wl_frame *previous;
};

/* Function table entry; defined and owned by the executor (exec/functions.c). */
struct wl_function;

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

    /* Positional parameters for the current function call or script. */
    struct wl_frame *frame;

    /* Options owned by `set`. */
    bool errexit;
    bool nounset;
    bool xtrace;
    bool noexec;

    /* Control flow. Counts are >0 only while unwinding. */
    int loop_depth;
    int break_count;
    int continue_count;
    bool returning;
    int return_status;
    int function_depth;
    int trap_depth;

    /*
     * >0 while running a condition or a tested branch of &&/||, where -e must
     * not fire. This is the pragmatic POSIX rule, not a full model of it.
     */
    int suppression;

    struct wl_function *functions;

    /* Trap actions, malloc'd command strings, NULL when unset. */
    char *trap_exit;
    char *trap_int;
    char *trap_term;

    /*
     * Source currently executing. Borrowed from main; used only to copy a
     * function definition into its own arena so it outlives the parse unit.
     */
    const char *source;
    size_t source_length;
};

void wl_shell_init(struct wl_shell *sh, bool plain, bool interactive);

/* Releases variables, frames and trap strings. */
void wl_shell_destroy(struct wl_shell *sh);

void wl_shell_set_status(struct wl_shell *sh, int status);

/* Records a status and requests that the loop stop (used by `exit`). */
void wl_shell_stop(struct wl_shell *sh, int status);

/*
 * Replaces the positional parameters of the current frame. Copies name0 and
 * args. Returns false on allocation failure.
 */
bool wl_shell_set_args(struct wl_shell *sh, const char *name0,
                       char *const *args, size_t count);

/* Pushes a frame for a function call. Returns false on allocation failure. */
bool wl_shell_push_frame(struct wl_shell *sh, const char *name0,
                         char *const *args, size_t count);

/* Pops the current frame (never the base frame). */
void wl_shell_pop_frame(struct wl_shell *sh);

/* Removes the first n positional parameters of the current frame. */
void wl_shell_shift(struct wl_shell *sh, size_t count);

#endif /* WL_CORE_SHELL_H */
