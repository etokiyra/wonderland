#include "builtins/builtins.h"

#include "core/limits.h"
#include "core/shell.h"
#include "core/var.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Builtins, M3a: cd, exit, export, unset.
 *
 * They own their state changes (cwd, PWD/OLDPWD, variables, loop termination)
 * and return a status like any command. No narrative, no aliases; those are
 * M6/M7 and belong in the same functions when they arrive.
 */

static bool wl_is_name(const char *text, size_t length) {
    if (length == 0) {
        return false;
    }
    if (!((text[0] >= 'a' && text[0] <= 'z') ||
          (text[0] >= 'A' && text[0] <= 'Z') || text[0] == '_')) {
        return false;
    }
    for (size_t index = 1; index < length; index++) {
        char c = text[index];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

static int wl_builtin_cd(struct wl_shell *sh, int argc, char **argv) {
    const char *target;
    char old_pwd[WL_PATH_MAX];
    char new_pwd[WL_PATH_MAX];
    bool print_new = false;

    if (argc > 2) {
        fprintf(stderr, "wonderland: cd: too many arguments\n");
        return 1;
    }

    if (argc == 2) {
        if (strcmp(argv[1], "-") == 0) {
            target = wl_var_get(&sh->vars, "OLDPWD", 6);
            if (target == NULL || target[0] == '\0') {
                fprintf(stderr, "wonderland: cd: OLDPWD not set\n");
                return 1;
            }
            print_new = true;
        } else {
            target = argv[1];
        }
    } else {
        target = wl_var_get(&sh->vars, "HOME", 4);
        if (target == NULL || target[0] == '\0') {
            fprintf(stderr, "wonderland: cd: HOME not set\n");
            return 1;
        }
    }

    old_pwd[0] = '\0';
    (void)getcwd(old_pwd, sizeof old_pwd);

    if (chdir(target) != 0) {
        fprintf(stderr, "wonderland: cd: %s: %s\n", target, strerror(errno));
        return 1;
    }

    if (getcwd(new_pwd, sizeof new_pwd) == NULL) {
        fprintf(stderr, "wonderland: cd: %s\n", strerror(errno));
        return 1;
    }

    if (print_new) {
        printf("%s\n", new_pwd);
    }

    if (old_pwd[0] != '\0') {
        (void)wl_var_set(&sh->vars, "OLDPWD", 6, old_pwd, strlen(old_pwd),
                         true);
    }
    if (!wl_var_set(&sh->vars, "PWD", 3, new_pwd, strlen(new_pwd), true)) {
        fprintf(stderr, "wonderland: cd: out of memory\n");
        return 1;
    }
    return 0;
}

/* POSIX: decimal, 0..255, nothing else. Returns false on any other form. */
static bool wl_parse_exit_status(const char *text, int *status) {
    char *end = NULL;
    long value;

    if (text == NULL || text[0] == '\0') {
        return false;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    if (value < 0 || value > 255) {
        return false;
    }
    *status = (int)value;
    return true;
}

static int wl_builtin_exit(struct wl_shell *sh, int argc, char **argv) {
    int status = sh->last_status;

    if (argc > 2) {
        fprintf(stderr, "wonderland: exit: too many arguments\n");
        return 1; /* do not exit: the argument list is ambiguous */
    }

    if (argc == 2 && !wl_parse_exit_status(argv[1], &status)) {
        fprintf(stderr, "wonderland: exit: %s: numeric argument required\n",
                argv[1]);
        wl_shell_stop(sh, 2);
        return 2;
    }

    wl_shell_stop(sh, status);
    return status;
}

static int wl_builtin_export(struct wl_shell *sh, int argc, char **argv) {
    int status = 0;

    if (argc == 1) {
        for (struct wl_var *variable = wl_vars_first(&sh->vars);
             variable != NULL; variable = variable->next) {
            if (variable->exported) {
                printf("export %s=%s\n", variable->name, variable->value);
            }
        }
        return 0;
    }

    for (int index = 1; index < argc; index++) {
        const char *argument = argv[index];
        const char *equal = strchr(argument, '=');
        size_t name_len = (equal != NULL) ? (size_t)(equal - argument)
                                          : strlen(argument);

        if (!wl_is_name(argument, name_len)) {
            fprintf(stderr, "wonderland: export: %.*s: not a valid identifier\n",
                    (int)name_len, argument);
            status = 1;
            continue;
        }
        if (equal != NULL) {
            if (!wl_var_set(&sh->vars, argument, name_len, equal + 1,
                            strlen(equal + 1), true)) {
                fprintf(stderr, "wonderland: export: out of memory\n");
                status = 1;
            }
        } else if (!wl_var_export(&sh->vars, argument, name_len)) {
            fprintf(stderr, "wonderland: export: out of memory\n");
            status = 1;
        }
    }
    return status;
}

static int wl_builtin_unset(struct wl_shell *sh, int argc, char **argv) {
    int status = 0;

    for (int index = 1; index < argc; index++) {
        if (!wl_is_name(argv[index], strlen(argv[index]))) {
            fprintf(stderr, "wonderland: unset: %s: not a valid identifier\n",
                    argv[index]);
            status = 1;
            continue;
        }
        (void)wl_var_unset(&sh->vars, argv[index], strlen(argv[index]));
    }
    return status;
}

struct wl_builtin {
    const char *name;
    int (*run)(struct wl_shell *sh, int argc, char **argv);
};

static const struct wl_builtin wl_builtins[] = {
    {"cd", wl_builtin_cd},
    {"exit", wl_builtin_exit},
    {"export", wl_builtin_export},
    {"unset", wl_builtin_unset},
};

bool wl_builtin_is(const char *name) {
    size_t count = sizeof wl_builtins / sizeof wl_builtins[0];

    for (size_t index = 0; index < count; index++) {
        if (strcmp(name, wl_builtins[index].name) == 0) {
            return true;
        }
    }
    return false;
}

bool wl_builtin_run(struct wl_shell *sh, int argc, char **argv) {
    size_t count = sizeof wl_builtins / sizeof wl_builtins[0];

    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        return false;
    }
    for (size_t index = 0; index < count; index++) {
        if (strcmp(argv[0], wl_builtins[index].name) == 0) {
            int status = wl_builtins[index].run(sh, argc, argv);

            wl_shell_set_status(sh, status);
            return true;
        }
    }
    return false;
}
