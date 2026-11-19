#ifndef WL_NARRATIVE_PERSONA_H
#define WL_NARRATIVE_PERSONA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * Narrative ABI: immutable persona data plus the selection state.
 *
 * Content packs are parsed by the shell side (src/content) into these
 * structures; libwl_narrative.a only reads them. Nothing here mentions
 * execution, jobs, or shell state, and the library still performs no I/O.
 */

/* One line of output, borrowed from the loaded pack. */
struct wl_line {
    const char *text;
    size_t len;
};

enum wl_event_kind {
    WL_EV_POST_EXEC = 0,
    WL_EV_SUCCESS,
    WL_EV_FAIL,
    WL_EV_SESSION_START,
    WL_EV_DIR_CHANGE,
    WL_EV_JOB_START,
    WL_EV_JOB_FINISH,
    WL_EV_EXIT,
    WL_EV_COUNT
};

#define WL_EVENT_BIT(kind) (1u << (unsigned)(kind))

struct wl_event {
    enum wl_event_kind kind;
    int status;        /* final status of the command        */
    int term_signal;   /* 0 when not signal-terminated       */
    unsigned cycle;    /* Alice's Library cycle (M7)         */
    time_t now;        /* caller's clock; tests inject it    */
    const char *cmdline;
    const char *argv0;
    const char *cwd;
};

enum wl_content_status {
    WL_CONTENT_STATUS_ORIGINAL = 0,
    WL_CONTENT_STATUS_VERIFIED,
    WL_CONTENT_STATUS_UNVERIFIED
};

enum wl_content_category {
    WL_CONTENT_CATEGORY_ORIGINAL = 0,
    WL_CONTENT_CATEGORY_CANONICAL,
    WL_CONTENT_CATEGORY_QUOTATION
};

struct wl_content_line {
    const char *id;
    const char *text;
    size_t len;
    unsigned event_mask;
    enum wl_content_status status;
    enum wl_content_category category;
    const char *author;
    const char *work;
    const char *source;
    const char *translation;
    const char *verified_by;
    const char *verified_on;
};

struct wl_persona {
    const char *id;
    const char *display_name;
    const char *class_name; /* main | major | secondary | guest */
    const char *voice;
    const char *worldview;
    const char *description;
    unsigned frequency;    /* per mille, 0..1000 */
    unsigned silence_bias; /* 0..100            */
    bool default_persona;
    unsigned event_mask;
    const struct wl_content_line *lines;
    size_t line_count;
};

struct wl_packset {
    const struct wl_persona *personas;
    size_t count;
};

#define WL_NARRATIVE_RECENT 25
#define WL_NARRATIVE_BUDGET 12
#define WL_NARRATIVE_BUDGET_SECONDS 600

struct wl_narrative_state {
    const struct wl_packset *packs;
    const struct wl_persona *persona;

    unsigned cursors[WL_EV_COUNT];
    time_t cooldowns[WL_EV_COUNT];
    time_t last_spoken;

    time_t budget_start;
    unsigned budget_count;

    const struct wl_content_line *recent[WL_NARRATIVE_RECENT];
    unsigned recent_count;
    unsigned recent_next;

    uint64_t rng;
};

void wl_narrative_state_init(struct wl_narrative_state *state);

/* Binds a pack set and selects its default persona (may be NULL). */
void wl_narrative_attach(struct wl_narrative_state *state,
                         const struct wl_packset *packs);

/* The active persona, or NULL. */
const struct wl_persona *wl_narrative_persona(
    const struct wl_narrative_state *state);

/* Switches by id or display name (case-insensitive). */
bool wl_narrative_possess(struct wl_narrative_state *state, const char *name);

/*
 * Returns true when the persona speaks, filling line with borrowed text.
 * Silence is normal: frequency, cooldowns, budget, anti-repetition, or no
 * candidate lines for the event.
 */
bool wl_narrative_react(struct wl_narrative_state *state,
                        const struct wl_event *event, struct wl_line *line);

/*
 * Renders a line's #{placeholders} into out (snprintf-style: returns the
 * length that would have been written). Unknown placeholders are copied
 * literally; the loader rejects them, so this is only defensiveness.
 */
size_t wl_narrative_render(const char *text, const struct wl_event *event,
                           const struct wl_persona *persona, char *out,
                           size_t capacity);

const char *wl_narrative_layer_name(void);

#endif /* WL_NARRATIVE_PERSONA_H */
