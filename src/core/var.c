#include "core/var.h"

#include <stdlib.h>
#include <string.h>

static struct wl_var *wl_var_find(const struct wl_vars *vars,
                                  const char *name, size_t name_len) {
    struct wl_var *variable;

    for (variable = vars->head; variable != NULL; variable = variable->next) {
        if (strlen(variable->name) == name_len &&
            memcmp(variable->name, name, name_len) == 0) {
            return variable;
        }
    }
    return NULL;
}

void wl_vars_init(struct wl_vars *vars, char **envp) {
    vars->head = NULL;
    if (envp == NULL) {
        return;
    }
    for (char **entry = envp; *entry != NULL; entry++) {
        const char *text = *entry;
        const char *equal = strchr(text, '=');

        if (equal == NULL) {
            continue;
        }
        (void)wl_var_set(vars, text, (size_t)(equal - text), equal + 1,
                         strlen(equal + 1), true);
    }
}

void wl_vars_free(struct wl_vars *vars) {
    struct wl_var *variable = vars->head;

    while (variable != NULL) {
        struct wl_var *next = variable->next;

        free(variable->name);
        free(variable->value);
        free(variable);
        variable = next;
    }
    vars->head = NULL;
}

const char *wl_var_get(const struct wl_vars *vars, const char *name,
                       size_t name_len) {
    struct wl_var *variable = wl_var_find(vars, name, name_len);

    return (variable != NULL) ? variable->value : NULL;
}

static bool wl_copy(char **destination, const char *data, size_t len) {
    char *copy = malloc(len + 1);

    if (copy == NULL) {
        return false;
    }
    memcpy(copy, data, len);
    copy[len] = '\0';
    *destination = copy;
    return true;
}

bool wl_var_set(struct wl_vars *vars, const char *name, size_t name_len,
                const char *value, size_t value_len, bool exported) {
    struct wl_var *variable = wl_var_find(vars, name, name_len);
    char *new_value = NULL;

    if (!wl_copy(&new_value, value, value_len)) {
        return false;
    }

    if (variable != NULL) {
        free(variable->value);
        variable->value = new_value;
        if (exported) {
            variable->exported = true;
        }
        return true;
    }

    variable = calloc(1, sizeof *variable);
    if (variable == NULL) {
        free(new_value);
        return false;
    }
    if (!wl_copy(&variable->name, name, name_len)) {
        free(new_value);
        free(variable);
        return false;
    }
    variable->value = new_value;
    variable->exported = exported;
    variable->next = vars->head;
    vars->head = variable;
    return true;
}

bool wl_var_unset(struct wl_vars *vars, const char *name, size_t name_len) {
    struct wl_var **link = &vars->head;

    while (*link != NULL) {
        struct wl_var *variable = *link;

        if (strlen(variable->name) == name_len &&
            memcmp(variable->name, name, name_len) == 0) {
            *link = variable->next;
            free(variable->name);
            free(variable->value);
            free(variable);
            return true;
        }
        link = &variable->next;
    }
    return false;
}

bool wl_var_export(struct wl_vars *vars, const char *name, size_t name_len) {
    struct wl_var *variable = wl_var_find(vars, name, name_len);

    if (variable != NULL) {
        variable->exported = true;
        return true;
    }
    return wl_var_set(vars, name, name_len, "", 0, true);
}

struct wl_var *wl_vars_first(struct wl_vars *vars) {
    return vars->head;
}

bool wl_var_is_exported(const struct wl_vars *vars, const char *name,
                        size_t name_len) {
    struct wl_var *variable = wl_var_find(vars, name, name_len);

    return variable != NULL && variable->exported;
}

char **wl_vars_build_environ(const struct wl_vars *vars) {
    size_t count = 0;
    char **environment;
    size_t at = 0;

    for (struct wl_var *variable = vars->head; variable != NULL;
         variable = variable->next) {
        if (variable->exported) {
            count++;
        }
    }

    environment = calloc(count + 1, sizeof *environment);
    if (environment == NULL) {
        return NULL;
    }

    for (struct wl_var *variable = vars->head; variable != NULL;
         variable = variable->next) {
        size_t name_len;
        size_t value_len;
        char *entry;

        if (!variable->exported) {
            continue;
        }
        name_len = strlen(variable->name);
        value_len = strlen(variable->value);
        entry = malloc(name_len + value_len + 2);
        if (entry == NULL) {
            wl_environ_free(environment);
            return NULL;
        }
        memcpy(entry, variable->name, name_len);
        entry[name_len] = '=';
        memcpy(entry + name_len + 1, variable->value, value_len);
        entry[name_len + value_len + 1] = '\0';
        environment[at++] = entry;
    }
    environment[at] = NULL;
    return environment;
}

void wl_environ_free(char **environment) {
    if (environment == NULL) {
        return;
    }
    for (char **entry = environment; *entry != NULL; entry++) {
        free(*entry);
    }
    free(environment);
}
