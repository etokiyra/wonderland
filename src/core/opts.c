#include "core/opts.h"

#include <stdio.h>
#include <string.h>

/*
 * Option parsing.
 *
 * Positional arguments after a script path, or after `-c`'s command string,
 * belong to the script/program: the first one is $0, the rest are $1... .
 * That is the POSIX convention and the only way to pass arguments to a script.
 *
 * The rule applied throughout: a request this build cannot serve is refused
 * loudly with a distinct exit code, never silently ignored
 * (docs/DECISIONS.md D-0026).
 */

static void wl_set_usage_error(struct wl_options *out, const char *message) {
    (void)snprintf(out->usage_error, sizeof out->usage_error, "%s", message);
}

enum wl_opt_action wl_opts_parse(int argc, char **argv, struct wl_options *out) {
    bool want_version = false;
    int command_index = -1;
    int script_index = -1;
    int stdin_index = -1;

    out->plain = false;
    out->command = NULL;
    out->script = NULL;
    out->stdin_stream = false;
    out->arg_start = 0;
    out->arg_count = 0;
    out->usage_error[0] = '\0';

    for (int index = 1; index < argc; index++) {
        const char *argument = argv[index];

        if (strcmp(argument, "--plain") == 0) {
            out->plain = true;
        } else if (strcmp(argument, "--version") == 0) {
            want_version = true;
        } else if (strcmp(argument, "-c") == 0) {
            if (index + 1 >= argc) {
                wl_set_usage_error(out, "option -c requires a command string");
                return WL_OPT_USAGE_ERROR;
            }
            index++;
            out->command = argv[index];
            command_index = index;
        } else if (argument[0] == '-' && argument[1] != '\0') {
            (void)snprintf(out->usage_error, sizeof out->usage_error,
                           "unknown option '%s'", argument);
            return WL_OPT_USAGE_ERROR;
        } else if (strcmp(argument, "-") == 0 && command_index < 0 &&
                   script_index < 0) {
            out->stdin_stream = true;
            stdin_index = index;
        } else if (command_index < 0 && script_index < 0) {
            out->script = argument;
            script_index = index;
        }
        /* Any further operands are arguments to the command or script. */
    }

    if (want_version) {
        return WL_OPT_VERSION;
    }

    if (out->command != NULL) {
        out->arg_start = (size_t)(command_index + 1);
        out->arg_count = (size_t)(argc - command_index - 1);
    } else if (out->script != NULL) {
        out->arg_start = (size_t)(script_index + 1);
        out->arg_count = (size_t)(argc - script_index - 1);
    } else if (out->stdin_stream) {
        out->arg_start = (size_t)(stdin_index + 1);
        out->arg_count = (size_t)(argc - stdin_index - 1);
    }

    /* Non-interactive execution never shows the world (amendment 2). */
    if (out->command != NULL || out->script != NULL || out->stdin_stream) {
        out->plain = true;
    }

    if (out->command != NULL) {
        return WL_OPT_COMMAND;
    }
    if (out->script != NULL) {
        return WL_OPT_SCRIPT;
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
