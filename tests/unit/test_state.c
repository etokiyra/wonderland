#include "core/state.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * Alice's Library state: round-trip fidelity, atomic-write permissions, and
 * the three failure modes that must never crash the shell (missing, corrupt,
 * wrong version).
 */

static char wl_dir[1024];
static char wl_path[1200];

static int wl_setup(void) {
    char template[] = "/tmp/wl-state-XXXXXX";

    if (mkdtemp(template) == NULL) {
        return 0;
    }
    (void)snprintf(wl_dir, sizeof wl_dir, "%s", template);
    (void)snprintf(wl_path, sizeof wl_path, "%s/state", wl_dir);
    return 1;
}

static void wl_cleanup(void) {
    (void)unlink(wl_path);
    (void)rmdir(wl_dir);
}

static void wl_fill_state(struct wl_state *state) {
    wl_state_init(state);
    state->cycle = 7;
    WL_ASSERT(wl_state_set_persona(state, "grimm"));
    state->has_rest = true;
    state->rest_cwd = strdup("/tmp/example");
    state->rest_time = (time_t)1795000000;
    WL_ASSERT(wl_state_set_var(state, "PATH", "/usr/bin:/bin"));
    WL_ASSERT(wl_state_set_var(state, "WEIRD", "line one\nline two"));
}

WL_TEST(round_trip_preserves_every_field) {
    struct wl_state saved;
    struct wl_state loaded;
    bool corrupt = true;
    struct stat info;

    WL_ASSERT(wl_setup());
    wl_fill_state(&saved);
    WL_ASSERT(wl_state_save(wl_path, &saved));

    WL_ASSERT(stat(wl_path, &info) == 0);
    WL_ASSERT_INT_EQ(info.st_mode & 0777, 0600);

    wl_state_init(&loaded);
    WL_ASSERT(wl_state_load(wl_path, &loaded, &corrupt));
    WL_ASSERT(!corrupt);
    WL_ASSERT_INT_EQ(loaded.cycle, 7);
    WL_ASSERT_STR_EQ(loaded.persona, "grimm");
    WL_ASSERT(loaded.has_rest);
    WL_ASSERT_STR_EQ(loaded.rest_cwd, "/tmp/example");
    WL_ASSERT_INT_EQ((long)loaded.rest_time, 1795000000L);
    WL_ASSERT_INT_EQ(loaded.var_count, 2);
    if (loaded.var_count == 2) {
        WL_ASSERT_STR_EQ(loaded.vars[0].name, "PATH");
        WL_ASSERT_STR_EQ(loaded.vars[0].value, "/usr/bin:/bin");
        WL_ASSERT_STR_EQ(loaded.vars[1].name, "WEIRD");
        WL_ASSERT_STR_EQ(loaded.vars[1].value, "line one\nline two");
    }

    wl_state_free(&saved);
    wl_state_free(&loaded);
    wl_cleanup();
}

WL_TEST(set_var_replaces_in_place) {
    struct wl_state state;

    wl_state_init(&state);
    WL_ASSERT(wl_state_set_var(&state, "PATH", "one"));
    WL_ASSERT(wl_state_set_var(&state, "PATH", "two"));
    WL_ASSERT_INT_EQ(state.var_count, 1);
    WL_ASSERT_STR_EQ(state.vars[0].value, "two");
    wl_state_free(&state);
}

WL_TEST(missing_file_is_not_corrupt) {
    struct wl_state state;
    bool corrupt = true;

    WL_ASSERT(wl_setup());
    wl_state_init(&state);
    WL_ASSERT(!wl_state_load(wl_path, &state, &corrupt));
    WL_ASSERT(!corrupt);
    WL_ASSERT_INT_EQ(state.cycle, 0);
    wl_state_free(&state);
    wl_cleanup();
}

WL_TEST(checksum_rejects_a_tampered_file) {
    struct wl_state saved;
    struct wl_state loaded;
    bool corrupt = false;
    FILE *file;
    char content[128];
    size_t length;

    WL_ASSERT(wl_setup());
    wl_fill_state(&saved);
    WL_ASSERT(wl_state_save(wl_path, &saved));

    /* Flip one digit in the cycle line without fixing the checksum. */
    file = fopen(wl_path, "r+");
    WL_ASSERT(file != NULL);
    if (file != NULL) {
        length = fread(content, 1, sizeof content - 1, file);
        content[length] = '\0';
        {
            char *cycle = strstr(content, "cycle 7");

            WL_ASSERT(cycle != NULL);
            if (cycle != NULL) {
                cycle[6] = '9';
            }
        }
        rewind(file);
        WL_ASSERT(fwrite(content, 1, length, file) == length);
        (void)fclose(file);
    }

    wl_state_init(&loaded);
    WL_ASSERT(!wl_state_load(wl_path, &loaded, &corrupt));
    WL_ASSERT(corrupt);
    WL_ASSERT_INT_EQ(loaded.cycle, 0);
    WL_ASSERT(loaded.persona == NULL);

    wl_state_free(&saved);
    wl_state_free(&loaded);
    wl_cleanup();
}

WL_TEST(wrong_version_and_truncation_are_corrupt) {
    struct wl_state state;
    bool corrupt = false;
    FILE *file;

    WL_ASSERT(wl_setup());
    file = fopen(wl_path, "w");
    WL_ASSERT(file != NULL);
    if (file != NULL) {
        (void)fputs("wonderland-state 2\ncycle 3\nchecksum 0000000000000000\n",
                    file);
        (void)fclose(file);
    }
    wl_state_init(&state);
    WL_ASSERT(!wl_state_load(wl_path, &state, &corrupt));
    WL_ASSERT(corrupt);
    wl_state_free(&state);

    (void)unlink(wl_path);
    file = fopen(wl_path, "w");
    WL_ASSERT(file != NULL);
    if (file != NULL) {
        (void)fputs("wonderland-state 1\ncycle 3\n", file);
        (void)fclose(file);
    }
    corrupt = false;
    wl_state_init(&state);
    WL_ASSERT(!wl_state_load(wl_path, &state, &corrupt));
    WL_ASSERT(corrupt);
    wl_state_free(&state);
    wl_cleanup();
}

WL_TEST(oversized_state_is_rejected_without_parsing) {
    struct wl_state state;
    bool corrupt = false;
    FILE *file;

    WL_ASSERT(wl_setup());
    file = fopen(wl_path, "w");
    WL_ASSERT(file != NULL);
    if (file != NULL) {
        for (int index = 0; index < 4000; index++) {
            (void)fputs("wonderland-state 1\n", file);
            (void)fputs("0123456789012345678901234567890123456789\n", file);
        }
        (void)fclose(file);
    }
    wl_state_init(&state);
    WL_ASSERT(!wl_state_load(wl_path, &state, &corrupt));
    WL_ASSERT(corrupt);
    wl_state_free(&state);
    wl_cleanup();
}

WL_TEST(invalid_ids_are_rejected_before_writing) {
    struct wl_state state;

    wl_state_init(&state);
    WL_ASSERT(!wl_state_set_persona(&state, "bad name"));
    WL_ASSERT(!wl_state_set_persona(&state, "bad\nname"));
    WL_ASSERT(state.persona == NULL);
    WL_ASSERT(!wl_state_set_var(&state, "1BAD", "x"));
    WL_ASSERT(!wl_state_set_var(&state, "BAD NAME", "x"));
    WL_ASSERT_INT_EQ(state.var_count, 0);
    wl_state_free(&state);
}

WL_TEST(clear_rest_forgets_the_checkpoint) {
    struct wl_state state;

    wl_fill_state(&state);
    wl_state_clear_rest(&state);
    WL_ASSERT(!state.has_rest);
    WL_ASSERT(state.rest_cwd == NULL);
    WL_ASSERT_INT_EQ(state.var_count, 0);
    WL_ASSERT_INT_EQ(state.cycle, 7);
    WL_ASSERT_STR_EQ(state.persona, "grimm");
    wl_state_free(&state);
}

void wl_suite_state(void) {
    wl_test_register("state: round trip", round_trip_preserves_every_field);
    wl_test_register("state: var replacement", set_var_replaces_in_place);
    wl_test_register("state: missing file", missing_file_is_not_corrupt);
    wl_test_register("state: bad checksum", checksum_rejects_a_tampered_file);
    wl_test_register("state: version/truncation",
                     wrong_version_and_truncation_are_corrupt);
    wl_test_register("state: oversized file",
                     oversized_state_is_rejected_without_parsing);
    wl_test_register("state: invalid ids", invalid_ids_are_rejected_before_writing);
    wl_test_register("state: clear rest", clear_rest_forgets_the_checkpoint);
}
