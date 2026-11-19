#include "line/read.h"

#include "jobs/signal.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

/*
 * Canonical-mode line reader.
 *
 * read(2) instead of stdio is deliberate: stdio may retry after EINTR, which
 * would swallow Ctrl-C. Here an interrupted read returns to the loop, the
 * interrupt flag is seen at the top, and the caller gets WL_LINE_INTERRUPTED.
 */

/* Discards the rest of an over-long line so the next read starts clean. */
static void wl_drain_line(int fd) {
    char discard[256];

    for (;;) {
        ssize_t count = read(fd, discard, sizeof discard);
        if (count <= 0) {
            return;
        }
        if (memchr(discard, '\n', (size_t)count) != NULL) {
            return;
        }
    }
}

enum wl_line_status wl_line_read(int fd, char *buf, size_t cap, size_t *len) {
    size_t used = 0;

    if (buf == NULL || cap == 0 || len == NULL) {
        return WL_LINE_ERROR;
    }

    for (;;) {
        ssize_t count;

        if (wl_signal_interrupt_pending() || wl_signal_term_pending()) {
            return WL_LINE_INTERRUPTED;
        }
        if (used + 1 >= cap) {
            wl_drain_line(fd);
            return WL_LINE_TOO_LONG;
        }

        count = read(fd, buf + used, cap - 1 - used);
        if (count < 0) {
            if (errno == EINTR) {
                return WL_LINE_INTERRUPTED;
            }
            return WL_LINE_ERROR;
        }
        if (count == 0) {
            buf[used] = '\0';
            *len = used;
            return used == 0 ? WL_LINE_EOF : WL_LINE_OK;
        }

        used += (size_t)count;
        {
            char *newline = memchr(buf, '\n', used);

            if (newline != NULL) {
                size_t line_len = (size_t)(newline - buf);

                if (line_len > 0 && buf[line_len - 1] == '\r') {
                    line_len--;
                }
                buf[line_len] = '\0';
                *len = line_len;
                return WL_LINE_OK;
            }
        }
    }
}
