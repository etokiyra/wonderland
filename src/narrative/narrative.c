#include "narrative.h"
#include "persona.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/*
 * Persona engine.
 *
 * Reads immutable pack data and decides whether the active persona speaks.
 * Deterministic on purpose: candidates are cycled in pack order with a
 * per-event cursor, and the only randomness is the frequency roll, driven by
 * a seeded xorshift generator. Tests can therefore assert exact behaviour, and
 * a surprising line can be reproduced.
 *
 * The library performs no I/O and allocates nothing; the shell owns packs,
 * state and output.
 */

#define WL_GLOBAL_COOLDOWN 10

const char *wl_narrative_layer_name(void) {
    return "narrative";
}

static void wl_state_clear_selection(struct wl_narrative_state *state) {
    for (size_t index = 0; index < WL_EV_COUNT; index++) {
        state->cursors[index] = (unsigned)-1;
    }
    memset(state->cooldowns, 0, sizeof state->cooldowns);
    state->last_spoken = 0;
    state->budget_start = 0;
    state->budget_count = 0;
    state->recent_count = 0;
    state->recent_next = 0;
    memset(state->recent, 0, sizeof state->recent);
}

void wl_narrative_state_init(struct wl_narrative_state *state) {
    memset(state, 0, sizeof *state);
    state->rng = 0x9e3779b97f4a7c15ULL;
}

static bool wl_name_equals(const char *left, const char *right) {
    if (left == NULL || right == NULL) {
        return false;
    }
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return false;
        }
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

void wl_narrative_attach(struct wl_narrative_state *state,
                         const struct wl_packset *packs) {
    const struct wl_persona *chosen = NULL;

    state->packs = packs;
    state->persona = NULL;
    wl_state_clear_selection(state);
    if (packs == NULL || packs->count == 0) {
        return;
    }

    for (size_t index = 0; index < packs->count; index++) {
        if (packs->personas[index].default_persona) {
            chosen = &packs->personas[index];
            break;
        }
    }
    if (chosen == NULL) {
        for (size_t index = 0; index < packs->count; index++) {
            if (wl_name_equals(packs->personas[index].id, "alice")) {
                chosen = &packs->personas[index];
                break;
            }
        }
    }
    if (chosen == NULL) {
        chosen = &packs->personas[0];
    }
    state->persona = chosen;
}

const struct wl_persona *wl_narrative_persona(
    const struct wl_narrative_state *state) {
    return state->persona;
}

bool wl_narrative_possess(struct wl_narrative_state *state, const char *name) {
    if (state->packs == NULL || name == NULL) {
        return false;
    }
    for (size_t index = 0; index < state->packs->count; index++) {
        const struct wl_persona *persona = &state->packs->personas[index];

        if (wl_name_equals(persona->id, name) ||
            wl_name_equals(persona->display_name, name)) {
            state->persona = persona;
            wl_state_clear_selection(state);
            return true;
        }
    }
    return false;
}

static unsigned wl_mask_for(enum wl_event_kind kind, int status) {
    if (kind == WL_EV_POST_EXEC) {
        return WL_EVENT_BIT(WL_EV_POST_EXEC) |
               WL_EVENT_BIT((status == 0) ? WL_EV_SUCCESS : WL_EV_FAIL);
    }
    return WL_EVENT_BIT(kind);
}

static bool wl_is_high_priority(enum wl_event_kind kind) {
    return kind == WL_EV_FAIL || kind == WL_EV_SESSION_START ||
           kind == WL_EV_EXIT;
}

static time_t wl_cooldown_seconds(enum wl_event_kind kind) {
    switch (kind) {
    case WL_EV_FAIL:
        return 45;
    case WL_EV_SUCCESS:
    case WL_EV_POST_EXEC:
        return 20;
    case WL_EV_DIR_CHANGE:
        return 60;
    case WL_EV_JOB_START:
    case WL_EV_JOB_FINISH:
        return 30;
    case WL_EV_SESSION_START:
    case WL_EV_EXIT:
    default:
        return 0;
    }
}

static bool wl_recent_contains(const struct wl_narrative_state *state,
                               const struct wl_content_line *line) {
    for (unsigned index = 0; index < state->recent_count; index++) {
        if (state->recent[index] == line) {
            return true;
        }
    }
    return false;
}

static void wl_recent_record(struct wl_narrative_state *state,
                             const struct wl_content_line *line) {
    state->recent[state->recent_next] = line;
    state->recent_next = (state->recent_next + 1) % WL_NARRATIVE_RECENT;
    if (state->recent_count < WL_NARRATIVE_RECENT) {
        state->recent_count++;
    }
}

static bool wl_roll(struct wl_narrative_state *state,
                    const struct wl_persona *persona) {
    unsigned probability = persona->frequency;
    uint64_t value;

    if (persona->silence_bias >= 100) {
        return false;
    }
    probability = probability * (100 - persona->silence_bias) / 100;
    if (probability == 0) {
        return false;
    }

    state->rng ^= state->rng << 13;
    state->rng ^= state->rng >> 7;
    state->rng ^= state->rng << 17;
    value = state->rng % 1000;
    return value < probability;
}

static const struct wl_content_line *wl_pick(
    struct wl_narrative_state *state, const struct wl_persona *persona,
    unsigned mask, enum wl_event_kind kind) {
    unsigned start;
    const struct wl_content_line *fallback = NULL;

    if (persona->line_count == 0) {
        return NULL;
    }
    start = (state->cursors[kind] + 1) % (unsigned)persona->line_count;
    for (size_t attempt = 0; attempt < persona->line_count; attempt++) {
        unsigned index =
            (start + (unsigned)attempt) % (unsigned)persona->line_count;
        const struct wl_content_line *line = &persona->lines[index];

        if ((line->event_mask & mask) == 0) {
            continue;
        }
        if (fallback == NULL) {
            fallback = line;
        }
        if (!wl_recent_contains(state, line)) {
            state->cursors[kind] = index;
            wl_recent_record(state, line);
            return line;
        }
    }
    /* Every candidate is recent: allow the first match rather than silence. */
    return fallback;
}

static void wl_render_value(char *out, size_t capacity, size_t *used,
                            const char *text) {
    while (text != NULL && *text != '\0') {
        unsigned char c = (unsigned char)*text++;

        if (c < 32 || c == 127) {
            c = '?';
        }
        if (*used + 1 < capacity) {
            out[(*used)++] = (char)c;
        }
    }
}

size_t wl_narrative_render(const char *text, const struct wl_event *event,
                           const struct wl_persona *persona, char *out,
                           size_t capacity) {
    size_t used = 0;
    const char *cursor = text;

    if (out == NULL || capacity == 0) {
        return 0;
    }
    while (cursor != NULL && *cursor != '\0') {
        if (cursor[0] == '#' && cursor[1] == '{') {
            const char *close = strchr(cursor + 2, '}');

            if (close != NULL) {
                size_t length = (size_t)(close - (cursor + 2));

                if (length == 8 &&
                    memcmp(cursor + 2, "cwd_base", 8) == 0) {
                    const char *base = NULL;
                    const char *cwd = (event != NULL) ? event->cwd : NULL;

                    if (cwd != NULL) {
                        base = strrchr(cwd, '/');
                        base = (base != NULL) ? base + 1 : cwd;
                    }
                    wl_render_value(out, capacity, &used, base);
                } else if (length == 3 &&
                           memcmp(cursor + 2, "cmd", 3) == 0) {
                    wl_render_value(out, capacity, &used,
                                    (event != NULL) ? event->argv0 : NULL);
                } else if (length == 6 &&
                           memcmp(cursor + 2, "status", 6) == 0) {
                    char number[32];

                    (void)snprintf(number, sizeof number, "%d",
                                   (event != NULL) ? event->status : 0);
                    wl_render_value(out, capacity, &used, number);
                } else if (length == 5 &&
                           memcmp(cursor + 2, "cycle", 5) == 0) {
                    char number[32];

                    (void)snprintf(number, sizeof number, "%u",
                                   (event != NULL) ? event->cycle : 0);
                    wl_render_value(out, capacity, &used, number);
                } else if (length == 7 &&
                           memcmp(cursor + 2, "persona", 7) == 0) {
                    wl_render_value(out, capacity, &used,
                                    (persona != NULL) ? persona->display_name
                                                      : NULL);
                } else {
                    /* Defensive: the loader rejects unknown placeholders. */
                    wl_render_value(out, capacity, &used, "#{");
                    for (size_t index = 0; index < length; index++) {
                        wl_render_value(out, capacity, &used,
                                        (char[]){cursor[2 + index], '\0'});
                    }
                    wl_render_value(out, capacity, &used, "}");
                }
                cursor = close + 1;
                continue;
            }
        }

        {
            unsigned char c = (unsigned char)*cursor++;

            if (c == '\n' || c == '\t') {
                if (used + 1 < capacity) {
                    out[used++] = (char)c;
                }
            } else if (c < 32 || c == 127) {
                if (used + 1 < capacity) {
                    out[used++] = '?';
                }
            } else if (used + 1 < capacity) {
                out[used++] = (char)c;
            }
        }
    }
    out[used] = '\0';
    return used;
}

bool wl_narrative_react(struct wl_narrative_state *state,
                        const struct wl_event *event, struct wl_line *line) {
    const struct wl_persona *persona;
    const struct wl_content_line *chosen;
    enum wl_event_kind effective;
    unsigned mask;
    time_t now;

    if (state == NULL || event == NULL || line == NULL) {
        return false;
    }
    line->text = NULL;
    line->len = 0;

    persona = state->persona;
    if (persona == NULL || persona->line_count == 0) {
        return false;
    }
    if (event->kind < 0 || event->kind >= WL_EV_COUNT) {
        return false;
    }
    now = event->now;
    effective = event->kind;
    if (effective == WL_EV_POST_EXEC) {
        effective = (event->status == 0) ? WL_EV_SUCCESS : WL_EV_FAIL;
    }
    mask = wl_mask_for(event->kind, event->status);

    if ((persona->event_mask & mask) == 0) {
        return false;
    }

    /* Budget: a shell that talks constantly is worse than a silent one. */
    if (event->kind != WL_EV_EXIT) {
        if (state->budget_start == 0 ||
            now < state->budget_start ||
            now - state->budget_start >= WL_NARRATIVE_BUDGET_SECONDS) {
            state->budget_start = now;
            state->budget_count = 0;
        }
        if (state->budget_count >= WL_NARRATIVE_BUDGET) {
            return false;
        }
    }

    /* Per-kind cooldowns apply to every event, including high priority. */
    if (now < state->cooldowns[effective]) {
        return false;
    }
    if (!wl_is_high_priority(effective)) {
        if (state->last_spoken != 0 && now >= state->last_spoken &&
            now - state->last_spoken < WL_GLOBAL_COOLDOWN) {
            return false;
        }
        if (!wl_roll(state, persona)) {
            return false;
        }
    }

    chosen = wl_pick(state, persona, mask, effective);
    if (chosen == NULL) {
        return false;
    }

    state->cooldowns[effective] = now + wl_cooldown_seconds(effective);
    state->last_spoken = now;
    if (event->kind != WL_EV_EXIT) {
        state->budget_count++;
    }
    line->text = chosen->text;
    line->len = chosen->len;
    return true;
}
