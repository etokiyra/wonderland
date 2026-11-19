#ifndef WL_CORE_DIAG_H
#define WL_CORE_DIAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/*
 * One syntax/lexical diagnostic per parse unit.
 *
 * `incomplete` means the input ended where more input could still produce a
 * valid program (unterminated quote, open $(, pending here-document, open
 * `if`). The interactive loop uses it to show a continuation prompt; scripts
 * treat it as a syntax error. This is the only place that decides the
 * difference between "needs more" and "wrong".
 */

struct wl_diag {
    bool set;
    bool incomplete;
    size_t offset; /* byte offset into the source */
    char message[256];
};

void wl_diag_clear(struct wl_diag *diag);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 4, 5)))
#endif
void wl_diag_set(struct wl_diag *diag, size_t offset, bool incomplete,
                 const char *format, ...);

/*
 * Prints `wonderland: name:line:col: message` followed by the offending
 * source line and a caret. Columns count bytes, not characters
 * (docs/DECISIONS.md D-0034).
 */
void wl_diag_print(const struct wl_diag *diag, const char *source,
                   size_t length, const char *name, FILE *out);

/* Computes 1-based line and byte-column and the bounds of that line. */
void wl_diag_locate(const char *source, size_t length, size_t offset,
                    size_t *line, size_t *column, size_t *line_start,
                    size_t *line_end);

#endif /* WL_CORE_DIAG_H */
