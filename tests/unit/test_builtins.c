#include "builtins/builtins.h"
#include "core/limits.h"
#include "core/shell.h"
#include "test.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Builtin behaviour, in process. cd changes the test runner's cwd, so the
 * fixture saves and restores it; stderr is silenced around expected errors so
 * the test output stays readable.
 */

static char wl_orig_cwd[WL_PATH_MAX];
static char wl_tmp[WL_PATH_MAX];
static char wl_orig_home[WL_PATH_MAX];
static int wl_had_home;
static int wl_saved_stderr = -1;
static int wl_ready;

static void wl_silence_stderr(void) {
    int devnull;

    fflush(stderr);
    wl_saved_stderr = dup(STDERR_FILENO);
    devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
        (void)dup2(devnull, STDERR_FILENO);
        (void)close(devnull);
    }
}

static void wl_restore_stderr(void) {
    fflush(stderr);
    if (wl_saved_stderr >= 0) {
        (void)dup2(wl_saved_stderr, STDERR_FILENO);
        (void)close(wl_saved_stderr);
        wl_saved_stderr = -1;
    }
}

static void wl_cleanup(void) {
    wl_restore_stderr();
    if (!wl_ready) {
        return;
    }
    (void)chdir(wl_orig_cwd);
    if (wl_had_home) {
        (void)setenv("HOME", wl_orig_home, 1);
    } else {
        (void)unsetenv("HOME");
    }
    (void)rmdir(wl_tmp);
}

static int wl_ensure_fixture(void) {
    char template[] = "/tmp/wl-builtins-XXXXXX";
    const char *home = getenv("HOME");

    if (wl_ready) {
        return 1;
    }
    if (getcwd(wl_orig_cwd, sizeof wl_orig_cwd) == NULL) {
        return 0;
    }
    if (home != NULL) {
        if (strlen(home) >= sizeof wl_orig_home) {
            return 0;
        }
        (void)snprintf(wl_orig_home, sizeof wl_orig_home, "%s", home);
        wl_had_home = 1;
    }
    if (mkdtemp(template) == NULL) {
        return 0;
    }
    (void)snprintf(wl_tmp, sizeof wl_tmp, "%s", template);
    wl_ready = 1;
    (void)atexit(wl_cleanup);
    return 1;
}

static struct wl_shell wl_new_shell(void) {
    struct wl_shell shell;

    wl_shell_init(&shell, false, false);
    return shell;
}

WL_TEST(cd_with_no_argument_uses_home) {
    struct wl_shell shell = wl_new_shell();
    char cwd[WL_PATH_MAX];
    char *argv[] = {"cd", NULL};

    WL_ASSERT(wl_ensure_fixture());
    WL_ASSERT(wl_var_set(&shell.vars, "HOME", 4, wl_tmp, strlen(wl_tmp), true));
    WL_ASSERT(wl_builtin_run(&shell, 1, argv));
    WL_ASSERT_INT_EQ(shell.last_status, 0);
    WL_ASSERT(getcwd(cwd, sizeof cwd) != NULL);
    WL_ASSERT_STR_EQ(cwd, wl_tmp);
    WL_ASSERT_STR_EQ(wl_var_get(&shell.vars, "PWD", 3), wl_tmp);
    wl_shell_destroy(&shell);
}

WL_TEST(cd_reports_failure) {
    struct wl_shell shell = wl_new_shell();
    char *argv[] = {"cd", "/definitely/not/a/directory", NULL};

    WL_ASSERT(wl_ensure_fixture());
    wl_silence_stderr();
    WL_ASSERT(wl_builtin_run(&shell, 2, argv));
    wl_restore_stderr();
    WL_ASSERT_INT_EQ(shell.last_status, 1);
    wl_shell_destroy(&shell);
}

WL_TEST(cd_rejects_too_many_arguments) {
    struct wl_shell shell = wl_new_shell();
    char *argv[] = {"cd", "/tmp", "/var", NULL};

    wl_silence_stderr();
    WL_ASSERT(wl_builtin_run(&shell, 3, argv));
    wl_restore_stderr();
    WL_ASSERT_INT_EQ(shell.last_status, 1);
    wl_shell_destroy(&shell);
}

WL_TEST(exit_uses_the_given_status) {
    struct wl_shell shell = wl_new_shell();
    char *argv[] = {"exit", "7", NULL};

    WL_ASSERT(wl_builtin_run(&shell, 2, argv));
    WL_ASSERT_INT_EQ(shell.last_status, 7);
    WL_ASSERT(!shell.running);
    wl_shell_destroy(&shell);
}

WL_TEST(exit_defaults_to_the_previous_status) {
    struct wl_shell shell = wl_new_shell();
    char *argv[] = {"exit", NULL};

    wl_shell_set_status(&shell, 42);
    WL_ASSERT(wl_builtin_run(&shell, 1, argv));
    WL_ASSERT_INT_EQ(shell.last_status, 42);
    WL_ASSERT(!shell.running);
    wl_shell_destroy(&shell);
}

WL_TEST(exit_rejects_non_numeric_arguments_and_stops) {
    struct wl_shell shell = wl_new_shell();
    char *argv[] = {"exit", "abc", NULL};

    wl_silence_stderr();
    WL_ASSERT(wl_builtin_run(&shell, 2, argv));
    wl_restore_stderr();
    WL_ASSERT_INT_EQ(shell.last_status, 2);
    WL_ASSERT(!shell.running);
    wl_shell_destroy(&shell);
}

WL_TEST(exit_refuses_ambiguous_argument_lists_without_stopping) {
    struct wl_shell shell = wl_new_shell();
    char *argv[] = {"exit", "1", "2", NULL};

    wl_silence_stderr();
    WL_ASSERT(wl_builtin_run(&shell, 3, argv));
    wl_restore_stderr();
    WL_ASSERT_INT_EQ(shell.last_status, 1);
    WL_ASSERT(shell.running);
    wl_shell_destroy(&shell);
}

WL_TEST(unknown_names_are_not_builtins) {
    struct wl_shell shell = wl_new_shell();
    char *argv[] = {"echo", "hi", NULL};

    WL_ASSERT(!wl_builtin_run(&shell, 2, argv));
    wl_shell_destroy(&shell);
}

void wl_suite_builtins(void) {
    wl_test_register("builtins: cd uses HOME", cd_with_no_argument_uses_home);
    wl_test_register("builtins: cd failure", cd_reports_failure);
    wl_test_register("builtins: cd too many arguments",
                     cd_rejects_too_many_arguments);
    wl_test_register("builtins: exit status", exit_uses_the_given_status);
    wl_test_register("builtins: exit defaults to $?", exit_defaults_to_the_previous_status);
    wl_test_register("builtins: exit rejects non-numeric",
                     exit_rejects_non_numeric_arguments_and_stops);
    wl_test_register("builtins: exit too many arguments",
                     exit_refuses_ambiguous_argument_lists_without_stopping);
    wl_test_register("builtins: unknown name", unknown_names_are_not_builtins);
}
