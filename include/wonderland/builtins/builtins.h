#ifndef WL_BUILTINS_BUILTINS_H
#define WL_BUILTINS_BUILTINS_H

#include <stdbool.h>

struct wl_shell;

/*
 * Builtin dispatch.
 *
 * Returns true when argv[0] names a builtin, in which case the builtin's
 * status has already been recorded in sh->last_status. Returns false when the
 * name is not a builtin and the caller should look for an external command.
 *
 * Builtins own every state change they represent: `cd` changes the working
 * directory and PWD/OLDPWD; `export`/`unset` own the variable store; `exit`
 * stops the loop. No other component may perform those changes
 * (docs/DECISIONS.md D-0005).
 */
bool wl_builtin_run(struct wl_shell *sh, int argc, char **argv);

/* True when the name is a builtin; lets the executor choose the path first. */
bool wl_builtin_is(const char *name);

#endif /* WL_BUILTINS_BUILTINS_H */
