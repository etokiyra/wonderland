#include "core/diag.h"

#include <stdarg.h>
#include <string.h>

void wl_diag_clear(struct wl_diag *diag) {
    diag->set = false;
    diag->incomplete = false;
    diag->offset = 0;
    diag->message[0] = '\0';
}

void wl_diag_set(struct wl_diag *diag, size_t offset, bool incomplete,
                 const char *format, ...) {
    va_list arguments;

    if (diag->set) {
        return; /* first error wins; later ones are usually noise */
    }
    diag->set = true;
    diag->incomplete = incomplete;
    diag->offset = offset;

    va_start(arguments, format);
    (void)vsnprintf(diag->message, sizeof diag->message, format, arguments);
    va_end(arguments);
}

void wl_diag_locate(const char *source, size_t length, size_t offset,
                    size_t *line, size_t *column, size_t *line_start,
                    size_t *line_end) {
    size_t current_line = 1;
    size_t start = 0;
    size_t end = 0;
    size_t position;

    if (offset > length) {
        offset = length;
    }

    for (position = 0; position < offset; position++) {
        if (source[position] == '\n') {
            current_line++;
            start = position + 1;
        }
    }

    end = start;
    while (end < length && source[end] != '\n') {
        end++;
    }

    if (line != NULL) {
        *line = current_line;
    }
    if (column != NULL) {
        *column = offset - start + 1;
    }
    if (line_start != NULL) {
        *line_start = start;
    }
    if (line_end != NULL) {
        *line_end = end;
    }
}

void wl_diag_print(const struct wl_diag *diag, const char *source,
                   size_t length, const char *name, FILE *out) {
    size_t line;
    size_t column;
    size_t line_start;
    size_t line_end;

    if (!diag->set) {
        return;
    }

    wl_diag_locate(source, length, diag->offset, &line, &column, &line_start,
                   &line_end);

    fprintf(out, "wonderland: %s:%zu:%zu: %s\n", name, line, column,
            diag->message);

    if (line_start < length) {
        size_t text_length = line_end - line_start;

        fputc(' ', out);
        fwrite(source + line_start, 1, text_length, out);
        fputc('\n', out);
        fputc(' ', out);
        for (size_t index = 0; index + 1 < column; index++) {
            fputc(source[line_start + index] == '\t' ? '\t' : ' ', out);
        }
        fputs("^\n", out);
    }
}
