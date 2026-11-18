#include "exec/path.h"

#include "core/limits.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * PATH resolution.
 *
 * Rules of consequence:
 *   - a name containing '/' is never searched, matching POSIX;
 *   - only regular files count, so a directory named like a command is skipped;
 *   - "found but not executable" is remembered and reported as such, so the
 *     caller can distinguish 126 from 127;
 *   - an empty PATH component means the current directory.
 */

#define WL_DEFAULT_PATH "/usr/local/bin:/usr/bin:/bin"

static bool wl_is_regular(const char *path) {
    struct stat info;

    if (stat(path, &info) != 0) {
        return false;
    }
    return S_ISREG(info.st_mode);
}

static bool wl_is_executable(const char *path) {
    return wl_is_regular(path) && access(path, X_OK) == 0;
}

static enum wl_path_status wl_find_direct(const char *name, char *out,
                                          size_t out_size) {
    if (!wl_is_regular(name)) {
        return WL_PATH_NOT_FOUND;
    }
    if (access(name, X_OK) != 0) {
        return WL_PATH_NOT_EXECUTABLE;
    }
    if (strlen(name) >= out_size) {
        return WL_PATH_ERROR;
    }
    memcpy(out, name, strlen(name) + 1);
    return WL_PATH_FOUND;
}

enum wl_path_status wl_path_find(const char *name, char *out, size_t out_size) {
    const char *path_env;
    const char *component;
    bool saw_nonexec = false;

    if (name == NULL || name[0] == '\0' || out == NULL || out_size == 0) {
        return WL_PATH_NOT_FOUND;
    }
    if (strchr(name, '/') != NULL) {
        return wl_find_direct(name, out, out_size);
    }

    path_env = getenv("PATH");
    if (path_env == NULL || path_env[0] == '\0') {
        path_env = WL_DEFAULT_PATH;
    }

    component = path_env;
    for (;;) {
        const char *colon = strchr(component, ':');
        size_t dir_len = (colon != NULL) ? (size_t)(colon - component)
                                         : strlen(component);
        char candidate[WL_PATH_MAX];
        int written;

        if (dir_len == 0) {
            written = snprintf(candidate, sizeof candidate, "./%s", name);
        } else {
            written = snprintf(candidate, sizeof candidate, "%.*s/%s",
                               (int)dir_len, component, name);
        }

        if (written > 0 && (size_t)written < sizeof candidate) {
            if (wl_is_executable(candidate)) {
                if ((size_t)written >= out_size) {
                    return WL_PATH_ERROR;
                }
                memcpy(out, candidate, (size_t)written + 1);
                return WL_PATH_FOUND;
            }
            if (wl_is_regular(candidate)) {
                saw_nonexec = true;
            }
        }

        if (colon == NULL) {
            break;
        }
        component = colon + 1;
    }

    return saw_nonexec ? WL_PATH_NOT_EXECUTABLE : WL_PATH_NOT_FOUND;
}
