#ifndef WL_EXEC_EXEC_H
#define WL_EXEC_EXEC_H

#include <stdbool.h>

struct wl_node;
struct wl_shell;

/*
 * Executes a parsed program.
 *
 * M2 supports the parts that do not need expansion, redirection or process
 * teardown: simple commands with literal words, `;` lists, `&&`/`||`, `!`, and
 * builtins. Constructs that the parser accepts but execution does not yet
 * support (pipelines with more than one command, redirections, assignments,
 * background `&`, compound commands) are refused with status 2 and one
 * diagnostic; they are never silently ignored.
 *
 * Returns false when execution could not continue (refused construct, out of
 * memory). The status has already been set in that case.
 */
bool wl_exec_program(struct wl_shell *shell, const struct wl_node *program);

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
int wl_exec_external(struct wl_shell *shell, char *const argv[]);

#endif /* WL_EXEC_EXEC_H */
