#ifndef WL_CORE_OPTS_H
#define WL_CORE_OPTS_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Command-line handling.
 *
 * Supported today: --plain, --version, -c STRING, and a lone "-" for stdin.
 * A script path is recognised so the error is honest, but it is not executed
 * until M3. Anything else is refused rather than ignored.
 *
 * Strings in the result are borrowed from argv and live as long as argv.
 */

enum wl_opt_action {
    /* Run the interactive loop, or read commands from stdin when not a tty. */
    WL_OPT_START = 0,

    /* Print the version line and exit successfully. */
    WL_OPT_VERSION,

    /* Execute the command string (opts.command). */
    WL_OPT_COMMAND,

    /* Execute the script file (opts.script); not implemented until M3. */
    WL_OPT_SCRIPT,

    /* Invalid invocation. `usage_error` explains why. */
    WL_OPT_USAGE_ERROR
};

struct wl_options {
    /* Plain mode: no narrative output, ever (docs/DESIGN.md §5). */
    bool plain;

    /* -c argument, or NULL. */
    const char *command;

    /* Script operand, or NULL. */
    const char *script;

    /* "-" was given: read stdin even if it is a terminal. */
    bool stdin_stream;

    /* Positional arguments: argv[arg_start] .. argv[arg_start+arg_count-1]. */
    size_t arg_start;
    size_t arg_count;

    /* Human-readable reason for WL_OPT_USAGE_ERROR; empty otherwise. */
    char usage_error[160];
};

/*
 * Parses argv (including argv[0]) into out. out is fully initialised, even on
 * error. `-c`, a script and stdin are non-interactive and therefore set plain
 * mode, as amendment 2 requires.
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
