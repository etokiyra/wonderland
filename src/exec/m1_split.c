#include "exec/m1_split.h"

#include <stdbool.h>

/*
 * M1-ONLY. This file is deleted in Milestone 2 (docs/DECISIONS.md D-0037).
 *
 * It splits on spaces and tabs and nothing else. No quotes, no escapes, no
 * expansion, no operators. Do not add them here: the real lexer exists for
 * that reason, and the whole point of this file being small and bad is that
 * deleting it is trivial.
 */

static bool wl_is_space(char c) {
    return c == ' ' || c == '\t';
}

size_t wl_m1_split(char *line, char **argv, size_t argv_cap) {
    size_t argc = 0;
    char *cursor;

    if (line == NULL || argv == NULL || argv_cap == 0) {
        return 0;
    }

    cursor = line;
    for (;;) {
        while (wl_is_space(*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        if (argc + 1 >= argv_cap) {
            argv[argv_cap - 1] = NULL;
            return argv_cap; /* saturated */
        }
        argv[argc] = cursor;
        argc++;
        while (*cursor != '\0' && !wl_is_space(*cursor)) {
            cursor++;
        }
        if (*cursor != '\0') {
            *cursor = '\0';
            cursor++;
        }
    }

    argv[argc] = NULL;
    return argc;
}
