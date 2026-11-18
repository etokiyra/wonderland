#include "exec/m1_split.h"
#include "test.h"

#include <string.h>

/*
 * The M1 splitter is disposable, but it is not allowed to be wrong about the
 * little it claims to do: whitespace separation, in-place pointers, saturation
 * instead of overflow.
 */

WL_TEST(splits_on_spaces_and_tabs) {
    char line[] = "  ls\t\t-l  ";
    char *argv[8];
    size_t argc = wl_m1_split(line, argv, 8);

    WL_ASSERT_INT_EQ(argc, 2);
    WL_ASSERT_STR_EQ(argv[0], "ls");
    WL_ASSERT_STR_EQ(argv[1], "-l");
    WL_ASSERT(argv[2] == NULL);
}

WL_TEST(empty_line_produces_no_words) {
    char line[] = "   \t ";
    char *argv[4];

    WL_ASSERT_INT_EQ(wl_m1_split(line, argv, 4), 0);
    WL_ASSERT(argv[0] == NULL);
}

WL_TEST(words_point_into_the_input_buffer) {
    char line[] = "echo hi";
    char *argv[4];

    WL_ASSERT_INT_EQ(wl_m1_split(line, argv, 4), 2);
    WL_ASSERT(argv[0] == line);
    WL_ASSERT(argv[1] == line + 5);
}

WL_TEST(no_empty_words_from_repeated_separators) {
    char line[] = "a    b\t\tc";
    char *argv[8];
    size_t argc = wl_m1_split(line, argv, 8);

    WL_ASSERT_INT_EQ(argc, 3);
    WL_ASSERT_STR_EQ(argv[0], "a");
    WL_ASSERT_STR_EQ(argv[1], "b");
    WL_ASSERT_STR_EQ(argv[2], "c");
}

WL_TEST(saturates_rather_than_overflowing) {
    char line[] = "a b c d e";
    char *argv[3];
    size_t argc = wl_m1_split(line, argv, 3);

    WL_ASSERT_INT_EQ(argc, 3);
    WL_ASSERT(argv[0] != NULL);
    WL_ASSERT(argv[1] != NULL);
    WL_ASSERT(argv[2] == NULL);
}

WL_TEST(single_slot_capacity_saturates_cleanly) {
    char line[] = "anything";
    char *argv[1];

    WL_ASSERT_INT_EQ(wl_m1_split(line, argv, 1), 1);
    WL_ASSERT(argv[0] == NULL);
}

WL_TEST(quoting_is_not_implemented_and_this_is_recorded) {
    /* M1 keeps quotes as ordinary characters; M2 replaces this behaviour. */
    char line[] = "echo \"a b\"";
    char *argv[8];
    size_t argc = wl_m1_split(line, argv, 8);

    WL_ASSERT_INT_EQ(argc, 3);
    WL_ASSERT_STR_EQ(argv[1], "\"a");
    WL_ASSERT_STR_EQ(argv[2], "b\"");
}

void wl_suite_split(void) {
    wl_test_register("split: spaces and tabs", splits_on_spaces_and_tabs);
    wl_test_register("split: empty line", empty_line_produces_no_words);
    wl_test_register("split: pointers into the buffer",
                     words_point_into_the_input_buffer);
    wl_test_register("split: repeated separators", no_empty_words_from_repeated_separators);
    wl_test_register("split: saturates at capacity", saturates_rather_than_overflowing);
    wl_test_register("split: single-slot capacity", single_slot_capacity_saturates_cleanly);
    wl_test_register("split: quotes are not special (M1)",
                     quoting_is_not_implemented_and_this_is_recorded);
}
