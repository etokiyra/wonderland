#ifndef WL_CORE_VAR_H
#define WL_CORE_VAR_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Shell variables and the exported environment.
 *
 * A small linked list rather than a hash table: the shell holds dozens of
 * variables, not thousands, and linear lookup keeps the code and the ownership
 * obvious. Values are malloc'd byte strings; no NUL inside a value.
 *
 * Names are validated by the caller (the parser already does this). The store
 * treats them as opaque byte ranges with a length.
 */

struct wl_var {
    char *name;
    char *value;
    bool exported;
    struct wl_var *next;
};

struct wl_vars {
    struct wl_var *head;
};

/* Copies envp entries of the form NAME=VALUE as exported variables. */
void wl_vars_init(struct wl_vars *vars, char **envp);
void wl_vars_free(struct wl_vars *vars);

/* Returns a borrowed NUL-terminated value, or NULL when unset. */
const char *wl_var_get(const struct wl_vars *vars, const char *name,
                       size_t name_len);

/*
 * Sets a variable. An existing exported flag is never cleared by a plain
 * assignment; passing exported=true exports it. Returns false on OOM.
 */
bool wl_var_set(struct wl_vars *vars, const char *name, size_t name_len,
                const char *value, size_t value_len, bool exported);

bool wl_var_unset(struct wl_vars *vars, const char *name, size_t name_len);

/* Marks an existing variable exported, creating an empty one if needed. */
bool wl_var_export(struct wl_vars *vars, const char *name, size_t name_len);

/* True when the variable exists and is exported. */
bool wl_var_is_exported(const struct wl_vars *vars, const char *name,
                        size_t name_len);

/* Iterates for `export -p` / debugging; no allocation. */
struct wl_var *wl_vars_first(struct wl_vars *vars);

/* Builds a NULL-terminated environment array of exported variables. */
char **wl_vars_build_environ(const struct wl_vars *vars);
void wl_environ_free(char **environment);

#endif /* WL_CORE_VAR_H */
