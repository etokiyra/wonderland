#ifndef WL_LINE_READ_H
#define WL_LINE_READ_H

#include <stddef.h>

/*
 * Canonical-mode line reader (M1).
 *
 * Reads exactly one line from fd using read(2), not stdio, so that SIGINT
 * reliably interrupts the wait: the handler sets a flag, read returns EINTR,
 * and the caller sees WL_LINE_INTERRUPTED. stdio would hide that behind its
 * own retry logic.
 *
 * Requires a terminal in canonical mode: POSIX guarantees at most one line
 * per read(2) in that mode, so no input is lost. The M5 line editor replaces
 * this file; it is not the beginning of an editor.
 */

enum wl_line_status {
    WL_LINE_OK = 0,
    WL_LINE_EOF,
    WL_LINE_INTERRUPTED,
    WL_LINE_TOO_LONG,
    WL_LINE_ERROR
};

/*
 * Reads one line into buf (capacity cap, including the NUL). The trailing
 * newline is removed, as is a trailing carriage return. On success *len is the
 * resulting length.
 */
enum wl_line_status wl_line_read(int fd, char *buf, size_t cap, size_t *len);

#endif /* WL_LINE_READ_H */
