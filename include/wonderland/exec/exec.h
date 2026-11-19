#ifndef WL_EXEC_EXEC_H
#define WL_EXEC_EXEC_H

#include <stdbool.h>

struct wl_node;
struct wl_shell;

/*
 * Executes a parsed program.
 *
 * Supported now: simple commands (with expansion, assignments, redirections,
 * here-documents), `;` lists, `&&`, `||`, `!`, and multi-stage pipelines.
 * Compound commands are still refused with status 2; that is M3b.
 *
 * Redirections are applied left to right in the child (or with save/restore
 * around a builtin). A failed redirection stops that command with status 1.
 *
 * Returns false when execution could not continue (refused construct, out of
 * memory). The status has already been set in that case.
 */
bool wl_exec_program(struct wl_shell *shell, const struct wl_node *program);

/* Frees the function table; call before wl_shell_destroy(). */
void wl_exec_shutdown(struct wl_shell *shell);

/* Runs the EXIT trap, if any, preserving the exit status unless it exits. */
void wl_exec_run_exit_trap(struct wl_shell *shell);

/* Runs the stored trap for signo (SIGINT/SIGTERM) if one is set. */
bool wl_exec_trap_run(struct wl_shell *shell, int signo);

/* Runs pending INT/TERM traps if their signal arrived. */
void wl_exec_trap_dispatch(struct wl_shell *shell);

#endif /* WL_EXEC_EXEC_H */
