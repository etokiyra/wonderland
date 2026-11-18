#ifndef WL_TERM_PROMPT_H
#define WL_TERM_PROMPT_H

#include <stddef.h>

/*
 * Formats the interactive prompt into buf.
 *
 * Pure function: no I/O, no shell state, so it is cheap to test. The caller
 * writes the result to the terminal (stderr), never to stdout.
 *
 * Shape: "wonderland:<cwd>$ " with "[status]" inserted when the previous
 * command failed. HOME is abbreviated to "~". A path that would make the
 * prompt unwieldy is reduced to its basename.
 *
 * Returns the length the prompt would have had (snprintf convention).
 */
size_t wl_prompt_format(char *buf, size_t cap, int last_status,
                        const char *cwd, const char *home);

#endif /* WL_TERM_PROMPT_H */
