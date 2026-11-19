#include "core/buf.h"

#include <stdlib.h>
#include <string.h>

static bool wl_buf_reserve(struct wl_buf *buf, size_t extra) {
    size_t needed;
    size_t capacity;
    char *grown;

    if (extra > (size_t)-1 - buf->len - 1) {
        return false;
    }
    needed = buf->len + extra + 1;
    if (needed <= buf->cap) {
        return true;
    }

    capacity = (buf->cap > 0) ? buf->cap : 32;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }

    grown = realloc(buf->data, capacity);
    if (grown == NULL) {
        return false;
    }
    buf->data = grown;
    buf->cap = capacity;
    return true;
}

void wl_buf_init(struct wl_buf *buf) {
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void wl_buf_free(struct wl_buf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void wl_buf_clear(struct wl_buf *buf) {
    buf->len = 0;
    if (buf->data != NULL) {
        buf->data[0] = '\0';
    }
}

bool wl_buf_append(struct wl_buf *buf, const char *data, size_t len) {
    if (len == 0) {
        return true;
    }
    if (!wl_buf_reserve(buf, len)) {
        return false;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return true;
}

bool wl_buf_append_char(struct wl_buf *buf, char c) {
    return wl_buf_append(buf, &c, 1);
}

bool wl_buf_append_str(struct wl_buf *buf, const char *text) {
    return wl_buf_append(buf, text, strlen(text));
}

char *wl_buf_take(struct wl_buf *buf) {
    char *data;

    if (buf->data == NULL) {
        data = malloc(1);
        if (data == NULL) {
            return NULL;
        }
        data[0] = '\0';
    } else {
        data = buf->data;
    }
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    return data;
}
