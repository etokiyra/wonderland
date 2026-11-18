#ifndef WL_EXEC_PATH_H
#define WL_EXEC_PATH_H

#include <stddef.h>

/*
 * PATH resolution.
 *
 * Separated from execution so the search rules are unit-testable without
 * forking. The result distinguishes "missing" from "present but not
 * executable", because the shell reports 127 and 126 respectively and a
 * single "failed" answer would lose that distinction.
 */

enum wl_path_status {
    WL_PATH_FOUND = 0,
    WL_PATH_NOT_FOUND,
    WL_PATH_NOT_EXECUTABLE,
    WL_PATH_ERROR /* e.g. the resolved path does not fit the buffer */
};

/*
 * Resolves name using $PATH (or a conventional default when PATH is unset).
 *
 * A name containing '/' is used directly and never searched. Only regular
 * files are considered. On WL_PATH_FOUND, out holds a NUL-terminated absolute
 * or explicitly relative path.
 */
enum wl_path_status wl_path_find(const char *name, char *out, size_t out_size);

#endif /* WL_EXEC_PATH_H */
