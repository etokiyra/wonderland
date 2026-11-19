#ifndef WL_EXPAND_ARITH_H
#define WL_EXPAND_ARITH_H

#include <stdbool.h>
#include <stddef.h>

struct wl_shell;

/*
 * Minimal POSIX arithmetic for $((...)): integers, + - * / %, unary signs,
 * parentheses, whitespace, decimal/hex literals, and bare variable names or
 * $name references resolved through the variable store. No assignments, no
 * comparisons, no floating point. Division or modulo by zero is an error.
 *
 * Returns false and fills `error` on a syntax or evaluation error.
 */
bool wl_arith_eval(struct wl_shell *shell, const char *source, size_t length,
                   long long *value, char *error, size_t error_size);

#endif /* WL_EXPAND_ARITH_H */
