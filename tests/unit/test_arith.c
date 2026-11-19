#include "core/shell.h"
#include "core/var.h"
#include "expand/arith.h"
#include "test.h"

#include <string.h>

/*
 * Arithmetic evaluator: precedence, parentheses, unary signs, bases,
 * variables, and the two errors that matter (syntax, division by zero).
 */

static bool eval(const char *text, long long *value, char *error,
                 size_t error_size) {
    struct wl_shell shell;
    bool ok;

    wl_shell_init(&shell, true, false);
    ok = wl_arith_eval(&shell, text, strlen(text), value, error, error_size);
    wl_shell_destroy(&shell);
    return ok;
}

WL_TEST(precedence_and_parentheses) {
    long long value = 0;
    char error[128];

    WL_ASSERT(eval("1 + 2 * 3", &value, error, sizeof error));
    WL_ASSERT_INT_EQ(value, 7);
    WL_ASSERT(eval("(1 + 2) * 3", &value, error, sizeof error));
    WL_ASSERT_INT_EQ(value, 9);
    WL_ASSERT(eval("10 / 3", &value, error, sizeof error));
    WL_ASSERT_INT_EQ(value, 3);
    WL_ASSERT(eval("10 % 3", &value, error, sizeof error));
    WL_ASSERT_INT_EQ(value, 1);
    WL_ASSERT(eval("-5 + 2", &value, error, sizeof error));
    WL_ASSERT_INT_EQ(value, -3);
    WL_ASSERT(eval("0x10 + 1", &value, error, sizeof error));
    WL_ASSERT_INT_EQ(value, 17);
}

WL_TEST(variables_are_looked_up) {
    struct wl_shell shell;
    long long value = 0;
    char error[128];

    wl_shell_init(&shell, true, false);
    WL_ASSERT(wl_var_set(&shell.vars, "X", 1, "4", 1, false));
    WL_ASSERT(wl_arith_eval(&shell, "X * 2", 5, &value, error, sizeof error));
    WL_ASSERT_INT_EQ(value, 8);
    WL_ASSERT(wl_arith_eval(&shell, "$X + 1", 6, &value, error, sizeof error));
    WL_ASSERT_INT_EQ(value, 5);
    wl_shell_destroy(&shell);
}

WL_TEST(errors_are_reported) {
    long long value = 0;
    char error[128];

    WL_ASSERT(!eval("1 / 0", &value, error, sizeof error));
    WL_ASSERT_CONTAINS(error, "division by zero");

    WL_ASSERT(!eval("1 +", &value, error, sizeof error));
    WL_ASSERT_CONTAINS(error, "arithmetic");

    WL_ASSERT(!eval("(1 + 2", &value, error, sizeof error));
    WL_ASSERT_CONTAINS(error, "expected ')'");
}

void wl_suite_arith(void) {
    wl_test_register("arith: precedence", precedence_and_parentheses);
    wl_test_register("arith: variables", variables_are_looked_up);
    wl_test_register("arith: errors", errors_are_reported);
}
