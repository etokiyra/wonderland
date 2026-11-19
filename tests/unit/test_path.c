#include "core/limits.h"
#include "exec/path.h"
#include "test.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * PATH resolution. The fixture lives in /tmp for the duration of the process
 * and is removed by an atexit handler, so a failing assertion still cleans up.
 */

static char wl_dir[WL_PATH_MAX];
static char wl_tool[WL_PATH_MAX + 8];
static char wl_noexec[WL_PATH_MAX + 8];
static char wl_subdir[WL_PATH_MAX + 8];
static char wl_saved_path[WL_PATH_MAX];
static char wl_saved_cwd[WL_PATH_MAX];
static int wl_had_path;
static int wl_ready;

static int wl_create_file(const char *path, mode_t mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);

    if (fd < 0) {
        return 0;
    }
    (void)close(fd);
    return chmod(path, mode) == 0;
}

static void wl_cleanup_fixture(void) {
    if (!wl_ready) {
        return;
    }
    if (wl_saved_cwd[0] != '\0') {
        (void)chdir(wl_saved_cwd);
    }
    (void)unlink(wl_tool);
    (void)unlink(wl_noexec);
    (void)rmdir(wl_subdir);
    (void)rmdir(wl_dir);
    if (wl_had_path) {
        (void)setenv("PATH", wl_saved_path, 1);
    } else {
        (void)unsetenv("PATH");
    }
}

static int wl_ensure_fixture(void) {
    char template[] = "/tmp/wl-path-XXXXXX";
    const char *path = getenv("PATH");

    if (wl_ready) {
        return 1;
    }
    if (getcwd(wl_saved_cwd, sizeof wl_saved_cwd) == NULL) {
        return 0;
    }
    if (path != NULL) {
        if (strlen(path) >= sizeof wl_saved_path) {
            return 0;
        }
        (void)snprintf(wl_saved_path, sizeof wl_saved_path, "%s", path);
        wl_had_path = 1;
    }

    if (mkdtemp(template) == NULL) {
        return 0;
    }
    (void)snprintf(wl_dir, sizeof wl_dir, "%s", template);
    (void)snprintf(wl_tool, sizeof wl_tool, "%s/tool", wl_dir);
    (void)snprintf(wl_noexec, sizeof wl_noexec, "%s/noexec", wl_dir);
    (void)snprintf(wl_subdir, sizeof wl_subdir, "%s/subdir", wl_dir);

    if (!wl_create_file(wl_tool, 0755) ||
        !wl_create_file(wl_noexec, 0644) || mkdir(wl_subdir, 0755) != 0) {
        return 0;
    }
    if (setenv("PATH", wl_dir, 1) != 0) {
        return 0;
    }

    wl_ready = 1;
    (void)atexit(wl_cleanup_fixture);
    return 1;
}

WL_TEST(finds_executable_in_path) {
    char resolved[WL_PATH_MAX] = "";

    WL_ASSERT(wl_ensure_fixture());
    WL_ASSERT_INT_EQ(wl_path_find("tool", resolved, sizeof resolved),
                     WL_PATH_FOUND);
    WL_ASSERT_STR_EQ(resolved, wl_tool);
}

WL_TEST(finds_explicit_relative_path) {
    char resolved[WL_PATH_MAX] = "";

    WL_ASSERT(wl_ensure_fixture());
    WL_ASSERT_INT_EQ(wl_path_find(wl_tool, resolved, sizeof resolved),
                     WL_PATH_FOUND);
    WL_ASSERT_STR_EQ(resolved, wl_tool);
}

WL_TEST(explicit_non_executable_is_reported_as_such) {
    char resolved[WL_PATH_MAX] = "";

    WL_ASSERT(wl_ensure_fixture());
    WL_ASSERT_INT_EQ(wl_path_find(wl_noexec, resolved, sizeof resolved),
                     WL_PATH_NOT_EXECUTABLE);
}

WL_TEST(missing_name_is_not_found) {
    char resolved[WL_PATH_MAX] = "";

    WL_ASSERT(wl_ensure_fixture());
    WL_ASSERT_INT_EQ(wl_path_find("no-such-command-xyz", resolved,
                                  sizeof resolved),
                     WL_PATH_NOT_FOUND);
}

WL_TEST(non_executable_in_path_is_reported_as_such) {
    char resolved[WL_PATH_MAX] = "";

    WL_ASSERT(wl_ensure_fixture());
    WL_ASSERT_INT_EQ(wl_path_find("noexec", resolved, sizeof resolved),
                     WL_PATH_NOT_EXECUTABLE);
}

WL_TEST(directory_is_not_a_command) {
    char resolved[WL_PATH_MAX] = "";

    WL_ASSERT(wl_ensure_fixture());
    WL_ASSERT_INT_EQ(wl_path_find("subdir", resolved, sizeof resolved),
                     WL_PATH_NOT_FOUND);
}

WL_TEST(empty_name_is_not_found) {
    char resolved[WL_PATH_MAX] = "";

    WL_ASSERT(wl_ensure_fixture());
    WL_ASSERT_INT_EQ(wl_path_find("", resolved, sizeof resolved),
                     WL_PATH_NOT_FOUND);
}

WL_TEST(small_output_buffer_is_an_error) {
    char resolved[2];

    WL_ASSERT(wl_ensure_fixture());
    WL_ASSERT_INT_EQ(wl_path_find("tool", resolved, sizeof resolved),
                     WL_PATH_ERROR);
}

WL_TEST(unset_path_does_not_find_fixture) {
    char resolved[WL_PATH_MAX] = "";

    WL_ASSERT(wl_ensure_fixture());
    WL_ASSERT_INT_EQ(unsetenv("PATH"), 0);
    WL_ASSERT_INT_EQ(wl_path_find("tool", resolved, sizeof resolved),
                     WL_PATH_NOT_FOUND);
    WL_ASSERT_INT_EQ(setenv("PATH", wl_dir, 1), 0);
}

void wl_suite_path(void) {
    wl_test_register("path: finds an executable", finds_executable_in_path);
    wl_test_register("path: explicit relative path", finds_explicit_relative_path);
    wl_test_register("path: explicit non-executable",
                     explicit_non_executable_is_reported_as_such);
    wl_test_register("path: missing name", missing_name_is_not_found);
    wl_test_register("path: non-executable in PATH",
                     non_executable_in_path_is_reported_as_such);
    wl_test_register("path: directory is not a command", directory_is_not_a_command);
    wl_test_register("path: empty name", empty_name_is_not_found);
    wl_test_register("path: output buffer too small", small_output_buffer_is_an_error);
    wl_test_register("path: unset PATH", unset_path_does_not_find_fixture);
}
