#ifndef WL_TEST_H
#define WL_TEST_H

/*
 * Minimal, dependency-free test harness.
 *
 * Usage:
 *
 *     static void my_case(void) {
 *         WL_ASSERT(1 + 1 == 2);
 *         WL_ASSERT_STR_EQ(wl_something(), "expected");
 *     }
 *
 *     void my_suite(void) {
 *         wl_test_register("my case", my_case);
 *     }
 *
 * A case "fails" if any assertion in it fails. wl_test_run() returns the
 * number of failed cases. There are no fixtures, no timeouts, and no magic:
 * if a case needs setup, it does the setup itself.
 */

#include <stddef.h>

typedef void (*wl_test_fn)(void);

/* Registers a case. Duplicate names are allowed; capacity is fixed. */
void wl_test_register(const char *name, wl_test_fn fn);

/* Records a failed assertion; called by the macros below. */
void wl_test_fail(const char *file, int line, const char *expression);

/* Runs every registered case. Returns the number of failed cases. */
int wl_test_run(void);

/* Value comparison helpers (kept out of macros so the header stays lean). */
int wl_test_str_eq(const char *left, const char *right);

/* Reports a mismatch with both values; used by the macros below. */
void wl_test_assert_str(const char *actual, const char *expected,
                        const char *file, int line);
void wl_test_assert_contains(const char *haystack, const char *needle,
                             const char *file, int line);

extern unsigned long wl_test_assertions;

#define WL_ASSERT(expr)                                                       \
    do {                                                                      \
        wl_test_assertions++;                                                 \
        if (!(expr)) {                                                        \
            wl_test_fail(__FILE__, __LINE__, #expr);                          \
        }                                                                     \
    } while (0)

#define WL_ASSERT_INT_EQ(actual, expected)                                    \
    do {                                                                      \
        long wl_test_a_ = (long)(actual);                                     \
        long wl_test_b_ = (long)(expected);                                   \
        wl_test_assertions++;                                                 \
        if (wl_test_a_ != wl_test_b_) {                                       \
            wl_test_fail(__FILE__, __LINE__, #actual " == " #expected);       \
        }                                                                     \
    } while (0)

#define WL_ASSERT_STR_EQ(actual, expected)                                    \
    do {                                                                      \
        wl_test_assertions++;                                                 \
        wl_test_assert_str((actual), (expected), __FILE__, __LINE__);         \
    } while (0)

#define WL_ASSERT_CONTAINS(haystack, needle)                                  \
    do {                                                                      \
        wl_test_assertions++;                                                 \
        wl_test_assert_contains((haystack), (needle), __FILE__, __LINE__);    \
    } while (0)

#define WL_TEST(name) static void name(void)

#endif /* WL_TEST_H */
