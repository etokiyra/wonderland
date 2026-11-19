#include "suites.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

#define WL_TEST_CAPACITY 256

struct wl_test_case {
    const char *name;
    wl_test_fn fn;
};

static struct wl_test_case wl_test_cases[WL_TEST_CAPACITY];
static size_t wl_test_count;
static int wl_current_failures;
static int wl_registration_overflow;

unsigned long wl_test_assertions;

void wl_test_register(const char *name, wl_test_fn fn) {
    if (wl_test_count >= WL_TEST_CAPACITY) {
        /* Never drop a case silently: the run must fail instead. */
        wl_registration_overflow = 1;
        return;
    }
    wl_test_cases[wl_test_count].name = name;
    wl_test_cases[wl_test_count].fn = fn;
    wl_test_count++;
}

void wl_test_fail(const char *file, int line, const char *expression) {
    wl_current_failures++;
    printf("%s:%d: assertion failed: %s\n", file, line, expression);
}

int wl_test_str_eq(const char *left, const char *right) {
    if (left == NULL || right == NULL) {
        return left == right;
    }
    return strcmp(left, right) == 0;
}

void wl_test_assert_str(const char *actual, const char *expected,
                        const char *file, int line) {
    if (wl_test_str_eq(actual, expected)) {
        return;
    }
    wl_current_failures++;
    printf("%s:%d: string mismatch\n  actual:   %s\n  expected: %s\n", file,
           line, (actual != NULL) ? actual : "(null)",
           (expected != NULL) ? expected : "(null)");
}

void wl_test_assert_contains(const char *haystack, const char *needle,
                             const char *file, int line) {
    if (haystack != NULL && needle != NULL && strstr(haystack, needle) != NULL) {
        return;
    }
    wl_current_failures++;
    printf("%s:%d: substring not found\n  needle:   %s\n  haystack: %s\n", file,
           line, (needle != NULL) ? needle : "(null)",
           (haystack != NULL) ? haystack : "(null)");
}

int wl_test_run(void) {
    int failed_cases = 0;

    if (wl_registration_overflow) {
        printf("FAIL test registration overflow (capacity %d)\n",
               WL_TEST_CAPACITY);
        return 1;
    }
    if (wl_test_count == 0) {
        printf("FAIL no tests registered\n");
        return 1;
    }

    for (size_t i = 0; i < wl_test_count; i++) {
        wl_current_failures = 0;
        wl_test_cases[i].fn();
        if (wl_current_failures == 0) {
            printf("ok   %s\n", wl_test_cases[i].name);
        } else {
            printf("FAIL %s (%d assertion%s)\n", wl_test_cases[i].name,
                   wl_current_failures,
                   wl_current_failures == 1 ? "" : "s");
            failed_cases++;
        }
    }

    printf("\n%zu case%s, %d failed, %lu assertion%s\n", wl_test_count,
           wl_test_count == 1 ? "" : "s", failed_cases, wl_test_assertions,
           wl_test_assertions == 1 ? "" : "s");

    return failed_cases;
}

int main(void) {
    wl_suite_opts();
    wl_suite_path();
    wl_suite_prompt();
    wl_suite_builtins();
    wl_suite_ast();
    wl_suite_fuzz();
    wl_suite_var();
    wl_suite_arith();
    wl_suite_history();
    return wl_test_run() == 0 ? 0 : 1;
}
