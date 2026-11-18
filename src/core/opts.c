#include "core/opts.h"

#include <stdio.h>
#include <string.h>

/*
 * Option parsing for the bootstrap binary.
 *
 * The rule applied throughout: a request this build cannot serve is refused
 * loudly with a distinct exit code, never silently ignored
 * (docs/DECISIONS.md D-0026).
 */

static void set_usage_error(struct wl_options *out, const char *message) {
    snprintf(out->usage_error, sizeof out->usage_error, "%s", message);
}

enum wl_opt_action wl_opts_parse(int argc, char **argv, struct wl_options *out) {
    bool want_version = false;
    bool unimplemented = false;

    out->plain = false;
    out->usage_error[0] = '\0';

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--plain") == 0) {
            out->plain = true;
        } else if (strcmp(arg, "--version") == 0) {
            want_version = true;
        } else if (strcmp(arg, "-c") == 0) {
            if (i + 1 >= argc) {
                set_usage_error(out, "option -c requires a command string");
                return WL_OPT_USAGE_ERROR;
            }
            i++;
            /* -c is always plain: scripts never see the world (amendment 2). */
            out->plain = true;
            unimplemented = true;
        } else if (arg[0] == '-' && arg[1] != '\0') {
            snprintf(out->usage_error, sizeof out->usage_error,
                     "unknown option '%s'", arg);
            return WL_OPT_USAGE_ERROR;
        } else {
            /* "-" (stdin) or a script path: always plain, not implemented. */
            out->plain = true;
            unimplemented = true;
        }
    }

    if (want_version) {
        return WL_OPT_VERSION;
    }
    if (unimplemented) {
        return WL_OPT_UNIMPLEMENTED;
    }
    return WL_OPT_START;
}

void wl_opts_apply_env(struct wl_options *out, const char *plain_env) {
    if (plain_env == NULL || plain_env[0] == '\0') {
        return;
    }
    if (strcmp(plain_env, "0") == 0) {
        return;
    }
    out->plain = true;
}
