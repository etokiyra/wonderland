#include "line/history.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * History storage. The file format is deliberately boring: one command per
 * line, no escapes. A command containing a newline is stored as several
 * entries, which is what every simple shell history does.
 */

static char *wl_history_duplicate(const char *text) {
    size_t length = strlen(text);
    char *copy = malloc(length + 1);

    if (copy != NULL) {
        memcpy(copy, text, length + 1);
    }
    return copy;
}

struct wl_history *wl_history_new(void) {
    return calloc(1, sizeof(struct wl_history));
}

void wl_history_free(struct wl_history *history) {
    if (history == NULL) {
        return;
    }
    for (size_t index = 0; index < history->count; index++) {
        free(history->entries[index]);
    }
    free(history->entries);
    free(history);
}

size_t wl_history_count(const struct wl_history *history) {
    return history->count;
}

const char *wl_history_at(const struct wl_history *history, size_t index) {
    return (index < history->count) ? history->entries[index] : "";
}

static bool wl_history_push(struct wl_history *history, const char *line) {
    char *copy;

    if (history->count == history->capacity) {
        size_t capacity = (history->capacity > 0) ? history->capacity * 2 : 64;
        char **grown = realloc(history->entries, capacity * sizeof *grown);

        if (grown == NULL) {
            return false;
        }
        history->entries = grown;
        history->capacity = capacity;
    }
    copy = wl_history_duplicate(line);
    if (copy == NULL) {
        return false;
    }
    history->entries[history->count++] = copy;
    return true;
}

void wl_history_add(struct wl_history *history, const char *line) {
    if (line == NULL || line[0] == '\0') {
        return;
    }
    if (history->count > 0 &&
        strcmp(history->entries[history->count - 1], line) == 0) {
        return;
    }
    (void)wl_history_push(history, line);
    while (history->count > WL_HISTORY_MAX) {
        free(history->entries[0]);
        memmove(history->entries, history->entries + 1,
                (history->count - 1) * sizeof *history->entries);
        history->count--;
    }
}

char *wl_history_default_path(void) {
    const char *state = getenv("XDG_STATE_HOME");
    const char *home = getenv("HOME");
    char *path;
    size_t length;

    if (state != NULL && state[0] != '\0') {
        length = strlen(state) + strlen("/wonderland/history") + 1;
        path = malloc(length);
        if (path != NULL) {
            (void)snprintf(path, length, "%s/wonderland/history", state);
        }
        return path;
    }
    if (home == NULL || home[0] == '\0') {
        return NULL;
    }
    length = strlen(home) + strlen("/.local/state/wonderland/history") + 1;
    path = malloc(length);
    if (path != NULL) {
        (void)snprintf(path, length, "%s/.local/state/wonderland/history",
                       home);
    }
    return path;
}

static bool wl_history_mkdirs(const char *path) {
    char *copy = wl_history_duplicate(path);

    if (copy == NULL) {
        return false;
    }
    for (char *cursor = copy + 1; *cursor != '\0'; cursor++) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (mkdir(copy, 0700) != 0 && errno != EEXIST) {
                free(copy);
                return false;
            }
            *cursor = '/';
        }
    }
    free(copy);
    return true;
}

bool wl_history_load(struct wl_history *history, const char *path) {
    FILE *file;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t count;

    if (path == NULL) {
        return false;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    while ((count = getline(&line, &capacity, file)) != -1) {
        while (count > 0 &&
               (line[count - 1] == '\n' || line[count - 1] == '\r')) {
            line[--count] = '\0';
        }
        if (count > 0) {
            (void)wl_history_push(history, line);
        }
    }
    free(line);
    (void)fclose(file);

    while (history->count > WL_HISTORY_MAX) {
        free(history->entries[0]);
        memmove(history->entries, history->entries + 1,
                (history->count - 1) * sizeof *history->entries);
        history->count--;
    }
    return true;
}

bool wl_history_save(const struct wl_history *history, const char *path) {
    FILE *file;
    size_t start = 0;

    if (path == NULL) {
        return false;
    }
    if (!wl_history_mkdirs(path)) {
        return false;
    }
    file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }
    (void)chmod(path, 0600);
    if (history->count > WL_HISTORY_MAX) {
        start = history->count - WL_HISTORY_MAX;
    }
    for (size_t index = start; index < history->count; index++) {
        if (fprintf(file, "%s\n", history->entries[index]) < 0) {
            (void)fclose(file);
            return false;
        }
    }
    return fclose(file) == 0;
}
