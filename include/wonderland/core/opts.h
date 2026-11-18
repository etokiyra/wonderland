#ifndef WL_CORE_OPTS_H
#define WL_CORE_OPTS_H

#include <stdbool.h>

/*
 * Command-line handling for the bootstrap binary.
 *
 * The parser is separated from main() so that option behaviour can be tested
 * without spawning a process (tests/unit/test_opts.c). It understands only
 * what the bootstrap supports; anything else is refused rather than ignored.
 */

enum wl_opt_action {
    /* No operands: start the shell (unimplemented in the bootstrap). */
    WL_OPT_START = 0,

    /* Print the version line and exit successfully. */
    WL_OPT_VERSION,

    /* A recognised request that this build cannot serve: -c, a script path,
     * or stdin. Reported on stderr with WL_EXIT_UNIMPLEMENTED. */
    WL_OPT_UNIMPLEMENTED,

    /* Invalid invocation. `usage_error` explains why. */
    WL_OPT_USAGE_ERROR
};

struct wl_options {
    /* Plain mode: no narrative output, ever (docs/DESIGN.md §5). */
    bool plain;

    /* Human-readable reason for WL_OPT_USAGE_ERROR; empty otherwise. */
    char usage_error[160];
};

/*
 * Parses argv (including argv[0]) into out. out is fully initialised, even on
 * error. Returns the action the caller should take.
 */
enum wl_opt_action wl_opts_parse(int argc, char **argv, struct wl_options *out);

/*
 * Applies the WONDERLAND_PLAIN environment value (`plain_env` may be NULL).
 * A set value other than "0" and other than the empty string enables plain
 * mode; there is no value that disables an already-enabled flag
 * (docs/DECISIONS.md D-0033).
 */
void wl_opts_apply_env(struct wl_options *out, const char *plain_env);

#endif /* WL_CORE_OPTS_H */
