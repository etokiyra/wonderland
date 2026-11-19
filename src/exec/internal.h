#ifndef WL_EXEC_INTERNAL_H
#define WL_EXEC_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

struct wl_shell;
struct wl_node;
struct wl_arena;

/* Executes one AST node. Used by the function and trap implementations. */
bool wl_exec_node(struct wl_shell *shell, const struct wl_node *node);

/*
 * Parses and executes a self-contained source string in the current shell.
 * Used for trap actions. Returns false on a syntax error (diagnostic printed).
 */
bool wl_exec_text(struct wl_shell *shell, const char *text);

/* Function table. */
bool wl_exec_function_define(struct wl_shell *shell,
                             const struct wl_node *definition);
bool wl_exec_function_exists(struct wl_shell *shell, const char *name,
                             size_t length);
bool wl_exec_function_call(struct wl_shell *shell, const char *name,
                           int argc, char **argv);
void wl_exec_function_free(struct wl_shell *shell);

/* Runs a stored trap action for the given signal (0 = EXIT). */
bool wl_exec_trap_run(struct wl_shell *shell, int signo);

/* Runs pending INT/TERM traps if their signal arrived. */
void wl_exec_trap_dispatch(struct wl_shell *shell);

#endif /* WL_EXEC_INTERNAL_H */
