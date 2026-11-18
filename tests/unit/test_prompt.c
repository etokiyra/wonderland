#include "term/prompt.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

/*
 * Prompt formatting is pure, so these tests assert exact bytes. The prompt is
 * the one place where "looks fine" is not a test.
 */

#define PROMPT_CAP 256

WL_TEST(prompt_abbreviates_home) {
    char buffer[PROMPT_CAP];

    WL_ASSERT_INT_EQ(wl_prompt_format(buffer, sizeof buffer, 0, "/home/u/proj",
                                      "/home/u"),
                     strlen("wonderland:~/proj $ "));
    WL_ASSERT_STR_EQ(buffer, "wonderland:~/proj $ ");
}

WL_TEST(prompt_home_itself_is_tilde) {
    char buffer[PROMPT_CAP];

    (void)wl_prompt_format(buffer, sizeof buffer, 0, "/home/u", "/home/u");
    WL_ASSERT_STR_EQ(buffer, "wonderland:~ $ ");
}

WL_TEST(prompt_shows_nonzero_status) {
    char buffer[PROMPT_CAP];

    (void)wl_prompt_format(buffer, sizeof buffer, 127, "/home/u/proj", "/home/u");
    WL_ASSERT_STR_EQ(buffer, "wonderland:~/proj [127] $ ");
}

WL_TEST(prompt_without_home_uses_full_path) {
    char buffer[PROMPT_CAP];

    (void)wl_prompt_format(buffer, sizeof buffer, 0, "/var/tmp", NULL);
    WL_ASSERT_STR_EQ(buffer, "wonderland:/var/tmp $ ");
}

WL_TEST(prompt_does_not_abbreviate_a_prefix_that_is_not_a_directory) {
    char buffer[PROMPT_CAP];

    (void)wl_prompt_format(buffer, sizeof buffer, 0, "/home/user2", "/home/u");
    WL_ASSERT_STR_EQ(buffer, "wonderland:/home/user2 $ ");
}

WL_TEST(long_paths_keep_the_leaf) {
    char cwd[PROMPT_CAP];
    char buffer[PROMPT_CAP];

    (void)snprintf(cwd, sizeof cwd,
                   "/home/u/aaaaaaaaaaaaaaaaaaaa/bbbbbbbbbbbbbbbbbbbb/"
                   "cccccccccccccccccccc/dddddddddddddddddddd/leaf");
    (void)wl_prompt_format(buffer, sizeof buffer, 0, cwd, "/home/u");
    WL_ASSERT(strstr(buffer, "../leaf") != NULL);
    WL_ASSERT(strlen(buffer) < 80);
}

WL_TEST(truncation_is_reported_and_terminated) {
    char buffer[8];
    size_t needed = wl_prompt_format(buffer, sizeof buffer, 0, "/tmp", NULL);

    WL_ASSERT(needed >= sizeof buffer);
    WL_ASSERT_INT_EQ(strlen(buffer), sizeof buffer - 1);
}

void wl_suite_prompt(void) {
    wl_test_register("prompt: HOME abbreviation", prompt_abbreviates_home);
    wl_test_register("prompt: HOME itself", prompt_home_itself_is_tilde);
    wl_test_register("prompt: nonzero status", prompt_shows_nonzero_status);
    wl_test_register("prompt: without HOME", prompt_without_home_uses_full_path);
    wl_test_register("prompt: no false prefix match",
                     prompt_does_not_abbreviate_a_prefix_that_is_not_a_directory);
    wl_test_register("prompt: long path keeps the leaf", long_paths_keep_the_leaf);
    wl_test_register("prompt: truncation reported", truncation_is_reported_and_terminated);
}
