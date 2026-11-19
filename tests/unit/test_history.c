#include "line/history.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * History: in-memory ordering, duplicate suppression, persistence and file
 * permissions, and the XDG state path.
 */

WL_TEST(ordering_and_duplicates) {
    struct wl_history *history = wl_history_new();

    WL_ASSERT(history != NULL);
    wl_history_add(history, "one");
    wl_history_add(history, "one");
    wl_history_add(history, "two");
    wl_history_add(history, "");
    WL_ASSERT_INT_EQ(wl_history_count(history), 2);
    WL_ASSERT_STR_EQ(wl_history_at(history, 0), "one");
    WL_ASSERT_STR_EQ(wl_history_at(history, 1), "two");
    wl_history_free(history);
}

WL_TEST(save_load_and_permissions) {
    char directory[] = "/tmp/wl-history-XXXXXX";
    char path[1024];
    struct wl_history *first;
    struct wl_history *second;
    struct stat info;

    WL_ASSERT(mkdtemp(directory) != NULL);
    (void)snprintf(path, sizeof path, "%s/state/wonderland/history", directory);

    first = wl_history_new();
    WL_ASSERT(first != NULL);
    wl_history_add(first, "alpha");
    wl_history_add(first, "beta gamma");
    WL_ASSERT(wl_history_save(first, path));
    WL_ASSERT_INT_EQ(stat(path, &info), 0);
    WL_ASSERT_INT_EQ(info.st_mode & 0777, 0600);
    wl_history_free(first);

    second = wl_history_new();
    WL_ASSERT(second != NULL);
    WL_ASSERT(wl_history_load(second, path));
    WL_ASSERT_INT_EQ(wl_history_count(second), 2);
    WL_ASSERT_STR_EQ(wl_history_at(second, 0), "alpha");
    WL_ASSERT_STR_EQ(wl_history_at(second, 1), "beta gamma");
    wl_history_free(second);

    (void)unlink(path);
    (void)rmdir("/tmp"); /* no-op guard: directories are below the temp dir */
    {
        char sub[1024];

        (void)snprintf(sub, sizeof sub, "%s/state/wonderland", directory);
        (void)rmdir(sub);
        (void)snprintf(sub, sizeof sub, "%s/state", directory);
        (void)rmdir(sub);
        (void)rmdir(directory);
    }
}

WL_TEST(default_path_uses_xdg_state_home) {
    const char *saved = getenv("XDG_STATE_HOME");
    char *saved_copy = (saved != NULL) ? strdup(saved) : NULL;
    char *path;

    WL_ASSERT(setenv("XDG_STATE_HOME", "/tmp/wl-xdg", 1) == 0);
    path = wl_history_default_path();
    WL_ASSERT(path != NULL);
    if (path != NULL) {
        WL_ASSERT_STR_EQ(path, "/tmp/wl-xdg/wonderland/history");
        free(path);
    }
    if (saved_copy != NULL) {
        (void)setenv("XDG_STATE_HOME", saved_copy, 1);
        free(saved_copy);
    } else {
        (void)unsetenv("XDG_STATE_HOME");
    }
}

void wl_suite_history(void) {
    wl_test_register("history: ordering and duplicates", ordering_and_duplicates);
    wl_test_register("history: save, load, 0600", save_load_and_permissions);
    wl_test_register("history: XDG path", default_path_uses_xdg_state_home);
}
