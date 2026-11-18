#include <stdio.h>
#include <stdlib.h>

#include "core/exit_code.h"
#include "core/opts.h"
#include "core/version.h"

/*
 * Bootstrap entry point.
 *
 * It answers --version, accepts --plain, and refuses everything else with a
 * distinct exit code. It does not read a line, does not fork, does not touch
 * the terminal. Milestone 1 replaces the "not implemented" branch with the
 * real loop (docs/ROADMAP.md).
 */

int main(int argc, char **argv) {
    struct wl_options opts;
    enum wl_opt_action action = wl_opts_parse(argc, argv, &opts);

    if (action == WL_OPT_USAGE_ERROR) {
        fprintf(stderr, "wonderland: %s\n", opts.usage_error);
        fprintf(stderr, "usage: wonderland [--plain] [--version]\n");
        return WL_EXIT_USAGE;
    }

    /* Plain mode is resolved before anything could observe the world. */
    wl_opts_apply_env(&opts, getenv("WONDERLAND_PLAIN"));

    if (action == WL_OPT_VERSION) {
        printf("%s\n", WL_VERSION_LINE);
        return WL_EXIT_OK;
    }

    if (action == WL_OPT_UNIMPLEMENTED) {
        fprintf(stderr,
                "wonderland: command execution is not implemented in this "
                "build; see docs/ROADMAP.md.\n");
        return WL_EXIT_UNIMPLEMENTED;
    }

    fprintf(stderr,
            "wonderland: the interactive shell is not implemented in this "
            "bootstrap build; see docs/ROADMAP.md.\n");
    return WL_EXIT_UNIMPLEMENTED;
}
