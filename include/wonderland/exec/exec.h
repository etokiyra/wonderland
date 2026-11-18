#ifndef WL_EXEC_EXEC_H
#define WL_EXEC_EXEC_H

#include <stdbool.h>

struct wl_shell;

/*
 * Executes one command line: split, dispatch to a builtin, or fork/exec.
 *
 * Returns false when the line contains no command (empty or whitespace only),
 * in which case $? is left unchanged. Otherwise updates sh->last_status and
 * returns true.
 */
bool wl_exec_line(struct wl_shell *sh, const char *line);

/*
 * Runs an external command with a NULL-terminated argv. Resolves via $PATH,
 * forks, hands the terminal to the child's process group when interactive,
 * waits, takes the terminal back, and returns the shell status:
 *
 *   127  command not found
 *   126  found but not executable, or exec failed
 *   128+N  terminated by signal N
 *   otherwise the child's exit status
 */
int wl_exec_external(struct wl_shell *sh, char *const argv[]);

#endif /* WL_EXEC_EXEC_H */
