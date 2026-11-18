#ifndef WL_EXEC_M1_SPLIT_H
#define WL_EXEC_M1_SPLIT_H

#include <stddef.h>

/*
 * M1-ONLY: the disposable whitespace splitter.
 *
 * This is not a lexer and must never grow into one. It exists so that
 * execution can be built and tested before the real lexer/parser (M2), and it
 * is deleted in M2 along with this header (docs/DECISIONS.md D-0037).
 *
 * What it deliberately does not do: quotes, escapes, expansion, comments,
 * semicolons, pipes, redirection, globbing, `;`, `&&`, `||`, `&`. If you need
 * any of those, you are past M1.
 *
 * Splits line in place on spaces and tabs and fills argv with pointers into
 * line. argv[argc] is set to NULL. Returns argc. If the input has more words
 * than fit, returns argv_cap (saturated) and still NUL-terminates argv.
 */
size_t wl_m1_split(char *line, char **argv, size_t argv_cap);

#endif /* WL_EXEC_M1_SPLIT_H */
