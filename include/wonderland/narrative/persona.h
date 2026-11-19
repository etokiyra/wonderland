#ifndef WL_NARRATIVE_PERSONA_H
#define WL_NARRATIVE_PERSONA_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Narrative ABI.
 *
 * This is the only surface the shell uses. It contains no shell types: an
 * event is immutable data, and the result is borrowed text that the *shell*
 * decides whether and where to write (docs/DECISIONS.md D-0015). The library
 * cannot reach execution state, and it performs no I/O.
 */

struct wl_line {
    const char *text; /* borrowed from the pack/table */
    size_t len;
};

enum wl_event_kind {
    WL_EV_POST_EXEC = 0,
    WL_EV_SESSION_START,
    WL_EV_EXIT
};

struct wl_event {
    enum wl_event_kind kind;
    int status;           /* final status of the command             */
    int term_signal;      /* 0 when not signal-terminated            */
    unsigned cycle;       /* Alice's Library cycle (M7)              */
    const char *cmdline;  /* sanitized, borrowed, may be NULL        */
    const char *argv0;    /* first word, borrowed, may be NULL       */
    const char *cwd;      /* borrowed, may be NULL                   */
};

#define WL_NARRATIVE_RECENT 4

struct wl_narrative_state {
    unsigned successes;
    unsigned failures;
    unsigned last_success;
    unsigned last_failure;
    unsigned last_exit;
    unsigned recent[WL_NARRATIVE_RECENT];
    unsigned recent_count;
    unsigned recent_next;
};

void wl_narrative_state_init(struct wl_narrative_state *state);

/*
 * Returns true when the persona speaks, filling line with borrowed text.
 * Silence is a normal outcome (success cadence, unknown event kind).
 */
bool wl_narrative_react(struct wl_narrative_state *state,
                        const struct wl_event *event, struct wl_line *line);

#endif /* WL_NARRATIVE_PERSONA_H */
