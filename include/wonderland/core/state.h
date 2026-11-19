#ifndef WL_CORE_STATE_H
#define WL_CORE_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/*
 * Alice's Library state.
 *
 * A small versioned text file under the XDG state directory, protected by an
 * FNV-1a checksum and written atomically (temp file + rename). It stores the
 * session cycle, the active persona, and the last `rest` checkpoint (cwd, a
 * whitelisted variable set, timestamp). It never stores processes, file
 * descriptors, or terminal state: `respawn` restores a shell snapshot, not a
 * machine.
 *
 * A missing file is normal. A corrupt or unknown-version file is ignored as a
 * whole; callers get `corrupt = true` and start from cycle 1.
 */

struct wl_state_var {
    char *name;
    char *value;
};

struct wl_state {
    unsigned cycle;
    char *persona; /* persona id, or NULL */

    bool has_rest;
    char *rest_cwd;
    time_t rest_time;

    struct wl_state_var *vars;
    size_t var_count;
    size_t var_capacity;
};

void wl_state_init(struct wl_state *state);
void wl_state_free(struct wl_state *state);

/* Replaces the rest checkpoint with an empty one. */
void wl_state_clear_rest(struct wl_state *state);

/* Replaces the recorded persona id (NULL clears it). False on OOM. */
bool wl_state_set_persona(struct wl_state *state, const char *id);

/* Adds or replaces a variable in the checkpoint. Returns false on OOM. */
bool wl_state_set_var(struct wl_state *state, const char *name,
                      const char *value);

/* $XDG_STATE_HOME/wonderland/state, fallback ~/.local/state/... */
char *wl_state_default_path(void);

/*
 * Returns true when a state file was read and validated. A missing file
 * returns false with `corrupt = false`; a bad checksum/version returns false
 * with `corrupt = true` and leaves the state empty.
 */
bool wl_state_load(const char *path, struct wl_state *state, bool *corrupt);

/* Atomically writes the state (creates directories, mode 0600). */
bool wl_state_save(const char *path, const struct wl_state *state);

#endif /* WL_CORE_STATE_H */
