#include "narrative/narrative.h"
#include "narrative/persona.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

/*
 * The narrative slice: Alice speaks on failures, every sixth success, and on
 * exit; she never repeats within the recent ring. No I/O is involved.
 */

WL_TEST(success_cadence_is_six) {
    struct wl_narrative_state state;
    struct wl_event event;
    struct wl_line line;
    int spoken = 0;

    wl_narrative_state_init(&state);
    memset(&event, 0, sizeof event);
    event.kind = WL_EV_POST_EXEC;
    event.status = 0;

    for (int index = 0; index < 12; index++) {
        if (wl_narrative_react(&state, &event, &line)) {
            spoken++;
            WL_ASSERT(line.text != NULL);
            WL_ASSERT(line.len > 0);
        }
    }
    WL_ASSERT_INT_EQ(spoken, 2);
}

WL_TEST(failures_always_speak_and_do_not_repeat) {
    struct wl_narrative_state state;
    struct wl_event event;
    struct wl_line line;
    char previous[256] = "";

    wl_narrative_state_init(&state);
    memset(&event, 0, sizeof event);
    event.kind = WL_EV_POST_EXEC;
    event.status = 1;

    for (int index = 0; index < 8; index++) {
        WL_ASSERT(wl_narrative_react(&state, &event, &line));
        WL_ASSERT(line.text != NULL);
        if (previous[0] != '\0') {
            WL_ASSERT(strcmp(previous, line.text) != 0);
        }
        (void)snprintf(previous, sizeof previous, "%s", line.text);
    }
}

WL_TEST(exit_and_unknown_events) {
    struct wl_narrative_state state;
    struct wl_event event;
    struct wl_line line;

    wl_narrative_state_init(&state);
    memset(&event, 0, sizeof event);
    event.kind = WL_EV_EXIT;
    WL_ASSERT(wl_narrative_react(&state, &event, &line));
    WL_ASSERT(line.len > 0);

    event.kind = WL_EV_SESSION_START;
    WL_ASSERT(!wl_narrative_react(&state, &event, &line));
    WL_ASSERT(line.text == NULL);
}

WL_TEST(layer_name_is_stable) {
    WL_ASSERT_STR_EQ(wl_narrative_layer_name(), "narrative");
}

void wl_suite_narrative(void) {
    wl_test_register("narrative: success cadence", success_cadence_is_six);
    wl_test_register("narrative: failures never repeat",
                     failures_always_speak_and_do_not_repeat);
    wl_test_register("narrative: exit and unknown events",
                     exit_and_unknown_events);
    wl_test_register("narrative: layer name", layer_name_is_stable);
}
