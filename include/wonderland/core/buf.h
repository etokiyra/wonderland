#ifndef WL_CORE_BUF_H
#define WL_CORE_BUF_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Growable byte buffer, NUL-terminated after every successful append.
 *
 * Used by the lexer (building literal runs), the AST dump, and the dump tests.
 * Allocation failure is reported, never ignored: callers must check the bool.
 * The buffer never shrinks; wl_buf_clear keeps capacity.
 */

struct wl_buf {
    char *data;
    size_t len;
    size_t cap;
};

void wl_buf_init(struct wl_buf *buf);

/* Frees storage and resets to empty. Safe on a zeroed struct and twice. */
void wl_buf_free(struct wl_buf *buf);

/* Keeps capacity but forgets the contents. */
void wl_buf_clear(struct wl_buf *buf);

bool wl_buf_append(struct wl_buf *buf, const char *data, size_t len);
bool wl_buf_append_char(struct wl_buf *buf, char c);
bool wl_buf_append_str(struct wl_buf *buf, const char *text);

/* Detaches the NUL-terminated contents. The caller owns them. */
char *wl_buf_take(struct wl_buf *buf);

#endif /* WL_CORE_BUF_H */
