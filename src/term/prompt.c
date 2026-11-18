#include "term/prompt.h"

#include <stdio.h>
#include <string.h>

/*
 * Prompt formatting. Pure: the caller supplies cwd and HOME, and decides where
 * the text is written (stderr, never stdout).
 */

#define WL_PROMPT_PATH_MAX 48
#define WL_PROMPT_BUFFER 192

static void wl_prompt_path(char *out, size_t cap, const char *cwd,
                           const char *home) {
    size_t home_len = (home != NULL) ? strlen(home) : 0;

    if (home_len > 0 && strncmp(cwd, home, home_len) == 0 &&
        (cwd[home_len] == '\0' || cwd[home_len] == '/')) {
        (void)snprintf(out, cap, "~%s", cwd + home_len);
    } else {
        (void)snprintf(out, cap, "%s", cwd);
    }

    if (strlen(out) <= WL_PROMPT_PATH_MAX) {
        return;
    }

    /* Too long for a prompt: keep the leaf, which is what people read. */
    const char *slash = strrchr(cwd, '/');
    if (slash != NULL && slash[1] != '\0') {
        (void)snprintf(out, cap, "../%s", slash + 1);
    }
}

size_t wl_prompt_format(char *buf, size_t cap, int last_status,
                        const char *cwd, const char *home) {
    char path[WL_PROMPT_BUFFER];

    wl_prompt_path(path, sizeof path, (cwd != NULL) ? cwd : "?", home);

    if (last_status != 0) {
        return (size_t)snprintf(buf, cap, "wonderland:%s [%d] $ ", path,
                                last_status);
    }
    return (size_t)snprintf(buf, cap, "wonderland:%s $ ", path);
}
