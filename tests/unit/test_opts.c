#include "core/opts.h"
#include "test.h"

#include <string.h>

/*
 * Option parsing is the only logic the bootstrap binary has, so it is tested
 * in both directions: what is accepted, and what is refused with which code.
 */

static int contains(const char *haystack, const char *needle) {
    return haystack != NULL && strstr(haystack, needle) != NULL;
}

WL_TEST(no_arguments_starts_the_shell) {
    char *argv[] = {"wonderland"};
    struct wl_options opts;

    WL_ASSERT_INT_EQ(wl_opts_parse(1, argv, &opts), WL_OPT_START);
    WL_ASSERT(!opts.plain);
    WL_ASSERT_STR_EQ(opts.usage_error, "");
}

WL_TEST(plain_flag_is_accepted) {
    char *argv[] = {"wonderland", "--plain"};
    struct wl_options opts;

    WL_ASSERT_INT_EQ(wl_opts_parse(2, argv, &opts), WL_OPT_START);
    WL_ASSERT(opts.plain);
}

WL_TEST(version_flag_prints_version) {
    char *argv[] = {"wonderland", "--version"};
    struct wl_options opts;

    WL_ASSERT_INT_EQ(wl_opts_parse(2, argv, &opts), WL_OPT_VERSION);
    WL_ASSERT(!opts.plain);
}

WL_TEST(version_wins_over_plain) {
    char *argv[] = {"wonderland", "--plain", "--version"};
    struct wl_options opts;

    WL_ASSERT_INT_EQ(wl_opts_parse(3, argv, &opts), WL_OPT_VERSION);
    WL_ASSERT(opts.plain);
}

WL_TEST(unknown_option_is_a_usage_error) {
    char *argv[] = {"wonderland", "--bogus"};
    struct wl_options opts;

    WL_ASSERT_INT_EQ(wl_opts_parse(2, argv, &opts), WL_OPT_USAGE_ERROR);
    WL_ASSERT(contains(opts.usage_error, "unknown option"));
    WL_ASSERT(contains(opts.usage_error, "--bogus"));
}

WL_TEST(short_unknown_option_is_a_usage_error) {
    char *argv[] = {"wonderland", "-z"};
    struct wl_options opts;

    WL_ASSERT_INT_EQ(wl_opts_parse(2, argv, &opts), WL_OPT_USAGE_ERROR);
    WL_ASSERT(contains(opts.usage_error, "-z"));
}

WL_TEST(command_string_is_unimplemented_and_plain) {
    char *argv[] = {"wonderland", "-c", "echo hi"};
    struct wl_options opts;

    WL_ASSERT_INT_EQ(wl_opts_parse(3, argv, &opts), WL_OPT_UNIMPLEMENTED);
    WL_ASSERT(opts.plain);
}

WL_TEST(command_string_requires_an_argument) {
    char *argv[] = {"wonderland", "-c"};
    struct wl_options opts;

    WL_ASSERT_INT_EQ(wl_opts_parse(2, argv, &opts), WL_OPT_USAGE_ERROR);
    WL_ASSERT(contains(opts.usage_error, "-c"));
}

WL_TEST(script_operand_is_unimplemented_and_plain) {
    char *argv[] = {"wonderland", "script.sh"};
    struct wl_options opts;

    WL_ASSERT_INT_EQ(wl_opts_parse(2, argv, &opts), WL_OPT_UNIMPLEMENTED);
    WL_ASSERT(opts.plain);
}

WL_TEST(stdin_operand_is_unimplemented_and_plain) {
    char *argv[] = {"wonderland", "-"};
    struct wl_options opts;

    WL_ASSERT_INT_EQ(wl_opts_parse(2, argv, &opts), WL_OPT_UNIMPLEMENTED);
    WL_ASSERT(opts.plain);
}

WL_TEST(env_plain_one_enables_plain) {
    struct wl_options opts = {.plain = false, .usage_error = ""};

    wl_opts_apply_env(&opts, "1");
    WL_ASSERT(opts.plain);
}

WL_TEST(env_plain_any_value_enables_plain) {
    struct wl_options opts = {.plain = false, .usage_error = ""};

    wl_opts_apply_env(&opts, "yes");
    WL_ASSERT(opts.plain);
}

WL_TEST(env_plain_zero_leaves_plain_off) {
    struct wl_options opts = {.plain = false, .usage_error = ""};

    wl_opts_apply_env(&opts, "0");
    WL_ASSERT(!opts.plain);
}

WL_TEST(env_plain_unset_or_empty_leaves_plain_off) {
    struct wl_options opts = {.plain = false, .usage_error = ""};

    wl_opts_apply_env(&opts, NULL);
    wl_opts_apply_env(&opts, "");
    WL_ASSERT(!opts.plain);
}

WL_TEST(env_plain_cannot_clear_the_flag) {
    struct wl_options opts = {.plain = true, .usage_error = ""};

    wl_opts_apply_env(&opts, "0");
    WL_ASSERT(opts.plain);
}

void wl_suite_opts(void) {
    wl_test_register("opts: no arguments starts the shell",
                     no_arguments_starts_the_shell);
    wl_test_register("opts: --plain is accepted", plain_flag_is_accepted);
    wl_test_register("opts: --version prints version", version_flag_prints_version);
    wl_test_register("opts: --version wins over --plain", version_wins_over_plain);
    wl_test_register("opts: unknown option is a usage error",
                     unknown_option_is_a_usage_error);
    wl_test_register("opts: unknown short option is a usage error",
                     short_unknown_option_is_a_usage_error);
    wl_test_register("opts: -c is unimplemented and plain",
                     command_string_is_unimplemented_and_plain);
    wl_test_register("opts: -c requires an argument",
                     command_string_requires_an_argument);
    wl_test_register("opts: script operand is unimplemented and plain",
                     script_operand_is_unimplemented_and_plain);
    wl_test_register("opts: stdin operand is unimplemented and plain",
                     stdin_operand_is_unimplemented_and_plain);
    wl_test_register("opts: WONDERLAND_PLAIN=1 enables plain",
                     env_plain_one_enables_plain);
    wl_test_register("opts: WONDERLAND_PLAIN=yes enables plain",
                     env_plain_any_value_enables_plain);
    wl_test_register("opts: WONDERLAND_PLAIN=0 leaves plain off",
                     env_plain_zero_leaves_plain_off);
    wl_test_register("opts: WONDERLAND_PLAIN unset or empty leaves plain off",
                     env_plain_unset_or_empty_leaves_plain_off);
    wl_test_register("opts: WONDERLAND_PLAIN cannot clear --plain",
                     env_plain_cannot_clear_the_flag);
}
