#include "core/var.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

/*
 * Variable store: lookup, overwrite, export, unset, and the environment
 * snapshot handed to children.
 */

WL_TEST(lookup_and_overwrite) {
    char *envp[] = {(char *)"A=1", (char *)"B=two", NULL};
    struct wl_vars vars;

    wl_vars_init(&vars, envp);
    WL_ASSERT_STR_EQ(wl_var_get(&vars, "A", 1), "1");
    WL_ASSERT_STR_EQ(wl_var_get(&vars, "B", 1), "two");
    WL_ASSERT(wl_var_get(&vars, "MISSING", 7) == NULL);

    WL_ASSERT(wl_var_set(&vars, "A", 1, "changed", 7, true));
    WL_ASSERT_STR_EQ(wl_var_get(&vars, "A", 1), "changed");
    WL_ASSERT(wl_var_is_exported(&vars, "A", 1));

    WL_ASSERT(wl_var_set(&vars, "NEW", 3, "x", 1, false));
    WL_ASSERT_STR_EQ(wl_var_get(&vars, "NEW", 3), "x");
    WL_ASSERT(!wl_var_is_exported(&vars, "NEW", 3));
    wl_vars_free(&vars);
}

WL_TEST(export_and_unset) {
    char *envp[] = {NULL};
    struct wl_vars vars;
    char **environment;

    wl_vars_init(&vars, envp);
    WL_ASSERT(wl_var_export(&vars, "KEEP", 4));
    WL_ASSERT_STR_EQ(wl_var_get(&vars, "KEEP", 4), "");
    WL_ASSERT(wl_var_is_exported(&vars, "KEEP", 4));

    WL_ASSERT(wl_var_set(&vars, "TEMP", 4, "value", 5, true));
    WL_ASSERT(wl_var_unset(&vars, "TEMP", 4));
    WL_ASSERT(wl_var_get(&vars, "TEMP", 4) == NULL);

    environment = wl_vars_build_environ(&vars);
    WL_ASSERT(environment != NULL);
    if (environment != NULL) {
        int found = 0;

        for (char **entry = environment; *entry != NULL; entry++) {
            if (strcmp(*entry, "KEEP=") == 0) {
                found = 1;
            }
            WL_ASSERT(strncmp(*entry, "TEMP=", 5) != 0);
        }
        WL_ASSERT(found);
        wl_environ_free(environment);
    }
    wl_vars_free(&vars);
}

WL_TEST(empty_environment_is_valid) {
    struct wl_vars vars;
    char **environment;

    wl_vars_init(&vars, NULL);
    WL_ASSERT(vars.head == NULL);
    environment = wl_vars_build_environ(&vars);
    WL_ASSERT(environment != NULL);
    if (environment != NULL) {
        WL_ASSERT(environment[0] == NULL);
        wl_environ_free(environment);
    }
    wl_vars_free(&vars);
}

void wl_suite_var(void) {
    wl_test_register("var: lookup and overwrite", lookup_and_overwrite);
    wl_test_register("var: export and unset", export_and_unset);
    wl_test_register("var: empty environment", empty_environment_is_valid);
}
