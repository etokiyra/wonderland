#include "builtins/builtins.h"

#include "core/limits.h"
#include "core/shell.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Builtins, M1: cd and exit only.
 *
 * They own their state changes (cwd, PWD/OLDPWD, loop termination) and return
 * a status like any command. No narrative, no confirmation, no aliases; those
 * are M6/M7 and belong in the same functions when they arrive.
 */

static int wl_builtin_cd(struct wl_shell *sh, int argc, char **argv) {
    const char *target;
    char old_pwd[WL_PATH_MAX];
    char new_pwd[WL_PATH_MAX];
    bool print_new = false;

    (void)sh;

    if (argc > 2) {
        fprintf(stderr, "wonderland: cd: too many arguments\n");
        return 1;
    }

    if (argc == 2) {
        if (strcmp(argv[1], "-") == 0) {
            target = getenv("OLDPWD");
            if (target == NULL || target[0] == '\0') {
                fprintf(stderr, "wonderland: cd: OLDPWD not set\n");
                return 1;
            }
            print_new = true;
        } else {
            target = argv[1];
        }
    } else {
        target = getenv("HOME");
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
        (void)setenv("OLDPWD", old_pwd, 1);
    }
    if (setenv("PWD", new_pwd, 1) != 0) {
        fprintf(stderr, "wonderland: cd: %s\n", strerror(errno));
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

struct wl_builtin {
    const char *name;
    int (*run)(struct wl_shell *sh, int argc, char **argv);
};

static const struct wl_builtin wl_builtins[] = {
    {"cd", wl_builtin_cd},
    {"exit", wl_builtin_exit},
};

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
