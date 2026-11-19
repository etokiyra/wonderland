#include "narrative.h"
#include "persona.h"

#include <string.h>

/*
 * Alice — the vertical-slice persona.
 *
 * Every line below is ORIGINAL writing for Wonderland. There is no Black Souls
 * dialogue and no quotation from any real person here; the persona is marked
 * original in the same sense as the pack format requires (D-0007, D-0040).
 *
 * The slice deliberately has no pack loader, no personality engine, no
 * cooldowns beyond a success cadence and a short no-repeat ring. It exists to
 * prove the wiring: immutable event in, borrowed text out, shell decides where
 * to print. The full engine is M6.
 */

static const char *const wl_success_lines[] = {
    "It held. The room pretends not to notice.",
    "One more turn of the key. Nothing followed you in.",
    "That worked. Try not to look relieved; the walls watch.",
    "You are learning the house rules faster than I did.",
    "Small victories count here. Small is all there is.",
    "The floor stayed where you left it. Good.",
};

static const char *const wl_fail_lines[] = {
    "Something refused you. Ask it again more politely.",
    "Not that way. The house is particular about doors.",
    "It broke? Or did it simply disagree with you?",
    "That sound means no. I would not push it twice.",
};

static const char *const wl_exit_lines[] = {
    "Leaving again. The door remembers you.",
    "Mind the step. There is always a step.",
};

const char *wl_narrative_layer_name(void) {
    return "narrative";
}

void wl_narrative_state_init(struct wl_narrative_state *state) {
    memset(state, 0, sizeof *state);
    state->last_success = (unsigned)-1;
    state->last_failure = (unsigned)-1;
    state->last_exit = (unsigned)-1;
}

static bool wl_recent_contains(const struct wl_narrative_state *state,
                               unsigned id) {
    for (unsigned index = 0; index < state->recent_count; index++) {
        if (state->recent[index] == id) {
            return true;
        }
    }
    return false;
}

static void wl_recent_record(struct wl_narrative_state *state, unsigned id) {
    state->recent[state->recent_next] = id;
    state->recent_next = (state->recent_next + 1) % WL_NARRATIVE_RECENT;
    if (state->recent_count < WL_NARRATIVE_RECENT) {
        state->recent_count++;
    }
}

static const char *wl_pick(struct wl_narrative_state *state,
                           const char *const *lines, unsigned count,
                           unsigned base, unsigned *last) {
    unsigned start;
    unsigned candidate = 0;

    if (count == 0) {
        return NULL;
    }
    start = (*last + 1) % count;
    candidate = start;
    for (unsigned attempt = 0; attempt < count; attempt++) {
        unsigned index = (start + attempt) % count;

        if (!wl_recent_contains(state, base + index)) {
            candidate = index;
            break;
        }
    }

    *last = candidate;
    wl_recent_record(state, base + candidate);
    return lines[candidate];
}

bool wl_narrative_react(struct wl_narrative_state *state,
                        const struct wl_event *event, struct wl_line *line) {
    const char *text = NULL;

    if (state == NULL || event == NULL || line == NULL) {
        return false;
    }
    line->text = NULL;
    line->len = 0;

    switch (event->kind) {
    case WL_EV_POST_EXEC:
        if (event->status == 0) {
            /*
             * Success is common; speaking every time would be noise. Every
             * sixth success is the slice's cadence (D-0092).
             */
            state->successes++;
            if ((state->successes % 6) != 0) {
                return false;
            }
            text = wl_pick(state, wl_success_lines,
                           (unsigned)(sizeof wl_success_lines /
                                      sizeof wl_success_lines[0]),
                           0, &state->last_success);
            if (text == NULL) {
                return false;
            }
        } else {
            state->failures++;
            text = wl_pick(state, wl_fail_lines,
                           (unsigned)(sizeof wl_fail_lines /
                                      sizeof wl_fail_lines[0]),
                           100, &state->last_failure);
            if (text == NULL) {
                return false;
            }
        }
        break;

    case WL_EV_EXIT:
        text = wl_pick(state, wl_exit_lines,
                       (unsigned)(sizeof wl_exit_lines /
                                  sizeof wl_exit_lines[0]),
                       200, &state->last_exit);
        if (text == NULL) {
            return false;
        }
        break;

    case WL_EV_SESSION_START:
    default:
        return false;
    }

    line->text = text;
    line->len = strlen(text);
    return true;
}
