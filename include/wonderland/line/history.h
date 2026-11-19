#ifndef WL_LINE_HISTORY_H
#define WL_LINE_HISTORY_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Command history.
 *
 * In-memory ring with the oldest entries dropped first; persisted as one line
 * per entry in a 0600 file under the XDG state directory. Loading and saving
 * are explicit so that non-interactive runs never touch the file. Corrupt or
 * unreadable history is ignored rather than fatal.
 */

#define WL_HISTORY_MAX 1000

struct wl_history {
    char **entries; /* oldest first */
    size_t count;
    size_t capacity;
};

struct wl_history *wl_history_new(void);
void wl_history_free(struct wl_history *history);

/* Adds a line unless empty or identical to the newest entry. */
void wl_history_add(struct wl_history *history, const char *line);

/* Number of entries and access by age: 0 is the oldest, count-1 the newest. */
size_t wl_history_count(const struct wl_history *history);
const char *wl_history_at(const struct wl_history *history, size_t index);

/* Loads a history file; missing files are not an error. */
bool wl_history_load(struct wl_history *history, const char *path);

/* Creates parent directories, then rewrites the file with mode 0600. */
bool wl_history_save(const struct wl_history *history, const char *path);

/*
 * Default path: $XDG_STATE_HOME/wonderland/history, falling back to
 * $HOME/.local/state/wonderland/history. Returns NULL when neither is set.
 */
char *wl_history_default_path(void);

#endif /* WL_LINE_HISTORY_H */
