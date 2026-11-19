#include "narrative/narrative.h"
#include "narrative/persona.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

/*
 * Engine tests with a synthetic pack: cadence, cooldowns, budget,
 * anti-repetition, possess and silence. No filesystem, no clock.
 */

static const struct wl_content_line wl_success_lines[2] = {
    {.id = "t.success.1",
     .text = "success-one",
     .len = 11,
     .event_mask = WL_EVENT_BIT(WL_EV_SUCCESS)},
    {.id = "t.success.2",
     .text = "success-two",
     .len = 11,
     .event_mask = WL_EVENT_BIT(WL_EV_SUCCESS)},
};

static const struct wl_content_line wl_fail_lines[2] = {
    {.id = "t.fail.1",
     .text = "fail-one",
     .len = 8,
     .event_mask = WL_EVENT_BIT(WL_EV_FAIL)},
    {.id = "t.fail.2",
     .text = "fail-two",
     .len = 8,
     .event_mask = WL_EVENT_BIT(WL_EV_FAIL)},
};

static const struct wl_content_line wl_exit_lines[1] = {
    {.id = "t.exit.1",
     .text = "exit-one",
     .len = 8,
     .event_mask = WL_EVENT_BIT(WL_EV_EXIT)},
};

static struct wl_persona wl_test_persona(void) {
    struct wl_persona persona;

    memset(&persona, 0, sizeof persona);
    persona.id = "test";
    persona.display_name = "Test";
    persona.class_name = "main";
    persona.frequency = 1000;
    persona.silence_bias = 0;
    persona.default_persona = true;
    persona.event_mask = WL_EVENT_BIT(WL_EV_SUCCESS) |
                         WL_EVENT_BIT(WL_EV_FAIL) |
                         WL_EVENT_BIT(WL_EV_EXIT);
    persona.lines = wl_success_lines;
    persona.line_count = 2;
    return persona;
}

static void wl_attach_lines(struct wl_narrative_state *state,
                            struct wl_persona *persona,
                            const struct wl_content_line *lines,
                            size_t count) {
    static struct wl_packset packs;

    persona->lines = lines;
    persona->line_count = count;
    packs.personas = persona;
    packs.count = 1;
    wl_narrative_state_init(state);
    wl_narrative_attach(state, &packs);
}

static void wl_event_init(struct wl_event *event, enum wl_event_kind kind,
                          int status, time_t now) {
    memset(event, 0, sizeof *event);
    event->kind = kind;
    event->status = status;
    event->now = now;
}

WL_TEST(attach_selects_the_default_persona) {
    struct wl_narrative_state state;
    struct wl_persona persona = wl_test_persona();

    wl_attach_lines(&state, &persona, wl_success_lines, 2);
    WL_ASSERT(wl_narrative_persona(&state) == &persona);
    WL_ASSERT_STR_EQ(wl_narrative_persona(&state)->id, "test");
}

WL_TEST(failures_always_speak) {
    struct wl_narrative_state state;
    struct wl_persona persona = wl_test_persona();
    struct wl_event event;
    struct wl_line line;

    wl_attach_lines(&state, &persona, wl_fail_lines, 2);
    wl_event_init(&event, WL_EV_POST_EXEC, 1, 100);
    WL_ASSERT(wl_narrative_react(&state, &event, &line));
    WL_ASSERT_STR_EQ(line.text, "fail-one");
}

WL_TEST(failures_obey_their_cooldown) {
    struct wl_narrative_state state;
    struct wl_persona persona = wl_test_persona();
    struct wl_event event;
    struct wl_line line;

    wl_attach_lines(&state, &persona, wl_fail_lines, 2);
    wl_event_init(&event, WL_EV_POST_EXEC, 1, 100);
    WL_ASSERT(wl_narrative_react(&state, &event, &line));

    event.now = 110;
    WL_ASSERT(!wl_narrative_react(&state, &event, &line));

    event.now = 146;
    WL_ASSERT(wl_narrative_react(&state, &event, &line));
    WL_ASSERT_STR_EQ(line.text, "fail-two");
}

WL_TEST(success_frequency_and_budget) {
    struct wl_narrative_state state;
    struct wl_persona persona = wl_test_persona();
    struct wl_event event;
    struct wl_line line;
    int spoken = 0;

    persona.frequency = 0;
    wl_attach_lines(&state, &persona, wl_success_lines, 2);
    wl_event_init(&event, WL_EV_POST_EXEC, 0, 100);
    WL_ASSERT(!wl_narrative_react(&state, &event, &line));

    persona.frequency = 1000;
    wl_attach_lines(&state, &persona, wl_fail_lines, 2);
    wl_event_init(&event, WL_EV_POST_EXEC, 1, 100);
    for (int index = 0; index < 13; index++) {
        event.now = 100 + index * 46;
        if (wl_narrative_react(&state, &event, &line)) {
            spoken++;
        }
    }
    WL_ASSERT_INT_EQ(spoken, 12);
}

WL_TEST(possess_switches_by_id_or_display_name) {
    struct wl_narrative_state state;
    struct wl_persona first = wl_test_persona();
    struct wl_persona second = wl_test_persona();
    struct wl_persona personas[2];
    struct wl_packset packs;

    second.id = "other";
    second.display_name = "Other";
    second.default_persona = false;
    personas[0] = first;
    personas[1] = second;
    packs.personas = personas;
    packs.count = 2;

    wl_narrative_state_init(&state);
    wl_narrative_attach(&state, &packs);
    WL_ASSERT(wl_narrative_possess(&state, "other"));
    WL_ASSERT_STR_EQ(wl_narrative_persona(&state)->id, "other");
    WL_ASSERT(wl_narrative_possess(&state, "TEST"));
    WL_ASSERT_STR_EQ(wl_narrative_persona(&state)->id, "test");
    WL_ASSERT(!wl_narrative_possess(&state, "missing"));
}

WL_TEST(exit_speaks_and_unknown_events_are_silent) {
    struct wl_narrative_state state;
    struct wl_persona persona = wl_test_persona();
    struct wl_event event;
    struct wl_line line;

    wl_attach_lines(&state, &persona, wl_exit_lines, 1);
    wl_event_init(&event, WL_EV_EXIT, 0, 100);
    WL_ASSERT(wl_narrative_react(&state, &event, &line));
    WL_ASSERT_STR_EQ(line.text, "exit-one");

    persona.event_mask = WL_EVENT_BIT(WL_EV_EXIT);
    wl_attach_lines(&state, &persona, wl_exit_lines, 1);
    wl_event_init(&event, WL_EV_DIR_CHANGE, 0, 100);
    WL_ASSERT(!wl_narrative_react(&state, &event, &line));
}

WL_TEST(layer_name_is_stable) {
    WL_ASSERT_STR_EQ(wl_narrative_layer_name(), "narrative");
}

WL_TEST(renders_placeholders) {
    struct wl_event event;
    struct wl_persona persona;
    char out[256];

    memset(&event, 0, sizeof event);
    memset(&persona, 0, sizeof persona);
    event.cwd = "/home/u/proj";
    event.argv0 = "make";
    event.status = 2;
    event.cycle = 3;
    persona.display_name = "Alice";

    WL_ASSERT_INT_EQ(wl_narrative_render(
                         "in #{cwd_base} #{cmd} #{status} #{cycle} "
                         "#{persona}",
                         &event, &persona, out, sizeof out),
                     strlen("in proj make 2 3 Alice"));
    WL_ASSERT_STR_EQ(out, "in proj make 2 3 Alice");
}

void wl_suite_narrative(void) {
    wl_test_register("narrative: default persona",
                     attach_selects_the_default_persona);
    wl_test_register("narrative: failures speak", failures_always_speak);
    wl_test_register("narrative: fail cooldown",
                     failures_obey_their_cooldown);
    wl_test_register("narrative: frequency and budget",
                     success_frequency_and_budget);
    wl_test_register("narrative: possess", possess_switches_by_id_or_display_name);
    wl_test_register("narrative: exit and silence",
                     exit_speaks_and_unknown_events_are_silent);
    wl_test_register("narrative: placeholders", renders_placeholders);
    wl_test_register("narrative: layer name", layer_name_is_stable);
}
