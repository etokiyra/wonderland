#include "term/prompt.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

WL_TEST(styled_prompt_uses_colour_and_icons) {
    char buffer[PROMPT_CAP];
    struct wl_prompt_style style = {true, true};

    (void)wl_prompt_format_ex(buffer, sizeof buffer, 0, "/home/u/proj",
                              "/home/u", "main", 2, 420, &style);
    WL_ASSERT_STR_EQ(buffer,
                     "wonderland:\x1b[1;36m \xef\x81\xbb ~/proj\x1b[0m"
                     "\x1b[35m \xee\x9c\xa5 main\x1b[0m"
                     "\x1b[33m \xef\x80\x93 2\x1b[0m"
                     "\x1b[2;33m \xef\x80\x97 0.42s\x1b[0m"
                     "\x1b[32m \xef\x80\x8c\x1b[0m"
                     "\x1b[1;32m \xef\x84\x85 \x1b[0m");
}

WL_TEST(styled_failure_is_red_and_keeps_the_code) {
    char buffer[PROMPT_CAP];
    struct wl_prompt_style style = {true, true};

    (void)wl_prompt_format_ex(buffer, sizeof buffer, 127, "/tmp", NULL, NULL,
                              0, 0, &style);
    WL_ASSERT_STR_EQ(buffer,
                     "wonderland:\x1b[1;36m \xef\x81\xbb /tmp\x1b[0m"
                     "\x1b[1;31m \xef\x80\x8d 127\x1b[0m"
                     "\x1b[1;31m \xef\x84\x85 \x1b[0m");
}

WL_TEST(ascii_fallback_has_no_escapes) {
    char buffer[PROMPT_CAP];
    struct wl_prompt_style style = {false, false};

    (void)wl_prompt_format_ex(buffer, sizeof buffer, 0, "/home/u/proj",
                              "/home/u", "main", 1, 150, &style);
    WL_ASSERT_STR_EQ(buffer, "wonderland:~/proj (main) [1 jobs] 0.15s $ ");
}

WL_TEST(visible_width_ignores_escapes_and_counts_icons_once) {
    WL_ASSERT_INT_EQ(wl_prompt_visible_width("\x1b[1;36m \xef\x81\xbb ~/x"
                                             "\x1b[0m > "),
                     9);
    WL_ASSERT_INT_EQ(wl_prompt_visible_width("wonderland:~/proj $ "),
                     strlen("wonderland:~/proj $ "));
}

static void wl_make_head_path(char *out, size_t cap, const char *directory) {
    size_t length = strlen(directory);

    if (length + 6 > cap) {
        out[0] = '\0';
        return;
    }
    memcpy(out, directory, length);
    memcpy(out + length, "/HEAD", 6);
}

WL_TEST(git_branch_is_read_from_dot_git) {
    char directory[] = "/tmp/wl-prompt-XXXXXX";
    char subdirectory[256];
    char head[256];
    char branch[64];
    FILE *file;

    WL_ASSERT(mkdtemp(directory) != NULL);
    (void)snprintf(subdirectory, sizeof subdirectory, "%s/.git", directory);
    WL_ASSERT(mkdir(subdirectory, 0700) == 0);
    wl_make_head_path(head, sizeof head, subdirectory);
    file = fopen(head, "w");
    WL_ASSERT(file != NULL);
    if (file != NULL) {
        (void)fputs("ref: refs/heads/feature/x\n", file);
        (void)fclose(file);
    }
    WL_ASSERT(wl_prompt_git(directory, branch, sizeof branch));
    WL_ASSERT_STR_EQ(branch, "feature/x");

    /* A subdirectory finds the same work tree. */
    (void)snprintf(head, sizeof head, "%s/sub", directory);
    WL_ASSERT(mkdir(head, 0700) == 0);
    WL_ASSERT(wl_prompt_git(head, branch, sizeof branch));
    WL_ASSERT_STR_EQ(branch, "feature/x");

    /* Detached HEAD becomes a short hash. */
    wl_make_head_path(head, sizeof head, subdirectory);
    file = fopen(head, "w");
    WL_ASSERT(file != NULL);
    if (file != NULL) {
        (void)fputs("0123456789abcdef0123456789abcdef01234567\n", file);
        (void)fclose(file);
    }
    WL_ASSERT(wl_prompt_git(directory, branch, sizeof branch));
    WL_ASSERT_STR_EQ(branch, "01234567");

    (void)snprintf(head, sizeof head, "%s/sub", directory);
    (void)rmdir(head);
    wl_make_head_path(head, sizeof head, subdirectory);
    (void)unlink(head);
    (void)rmdir(subdirectory);
    (void)rmdir(directory);
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
    wl_test_register("prompt: styled colour/icons",
                     styled_prompt_uses_colour_and_icons);
    wl_test_register("prompt: styled failure",
                     styled_failure_is_red_and_keeps_the_code);
    wl_test_register("prompt: ASCII fallback", ascii_fallback_has_no_escapes);
    wl_test_register("prompt: visible width",
                     visible_width_ignores_escapes_and_counts_icons_once);
    wl_test_register("prompt: git branch", git_branch_is_read_from_dot_git);
}
