#include "core/shell.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

/*
 * Session state. Deliberately small: fields appear when a milestone needs
 * them, not in anticipation of one.
 */

static char *wl_duplicate_text(const char *text) {
    size_t length;
    char *copy;

    if (text == NULL) {
        text = "";
    }
    length = strlen(text);
    copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, text, length + 1);
    }
    return copy;
}

static void wl_frame_clear(struct wl_frame *frame) {
    for (size_t index = 0; index < frame->count; index++) {
        free(frame->args[index]);
    }
    free(frame->args);
    free(frame->name0);
    frame->args = NULL;
    frame->count = 0;
    frame->name0 = NULL;
}

void wl_shell_init(struct wl_shell *sh, bool plain, bool interactive) {
    memset(sh, 0, sizeof *sh);
    sh->interactive = interactive;
    sh->plain = plain;
    sh->running = true;
    sh->last_status = 0;
    sh->tty_fd = interactive ? STDIN_FILENO : -1;
    sh->shell_pgid = getpgrp();
    wl_vars_init(&sh->vars, environ);
    sh->jobs.head = NULL;
    sh->jobs.next_id = 1;
    sh->jobs.started = 0;
    sh->jobs.finished = 0;
    sh->jobs.last_finished_command = NULL;
    wl_state_init(&sh->library);
    sh->library_path = NULL;
    sh->library_enabled = false;

    sh->frame = calloc(1, sizeof *sh->frame);
    if (sh->frame != NULL) {
        sh->frame->name0 = wl_duplicate_text("wonderland");
    }
}

void wl_shell_destroy(struct wl_shell *sh) {
    while (sh->frame != NULL && sh->frame->previous != NULL) {
        wl_shell_pop_frame(sh);
    }
    if (sh->frame != NULL) {
        wl_frame_clear(sh->frame);
        free(sh->frame);
        sh->frame = NULL;
    }
    free(sh->trap_exit);
    free(sh->trap_int);
    free(sh->trap_term);
    wl_state_free(&sh->library);
    free(sh->library_path);
    sh->library_path = NULL;
    wl_vars_free(&sh->vars);
}

void wl_shell_set_status(struct wl_shell *sh, int status) {
    sh->last_status = status;
}

void wl_shell_stop(struct wl_shell *sh, int status) {
    sh->last_status = status;
    sh->running = false;
}

static bool wl_frame_fill(struct wl_frame *frame, const char *name0,
                          char *const *args, size_t count) {
    frame->args = NULL;
    frame->count = 0;
    frame->name0 = wl_duplicate_text(name0);
    if (frame->name0 == NULL) {
        return false;
    }
    if (count == 0) {
        return true;
    }
    frame->args = calloc(count, sizeof *frame->args);
    if (frame->args == NULL) {
        free(frame->name0);
        frame->name0 = NULL;
        return false;
    }
    for (size_t index = 0; index < count; index++) {
        frame->args[index] = wl_duplicate_text(args[index]);
        if (frame->args[index] == NULL) {
            for (size_t done = 0; done < index; done++) {
                free(frame->args[done]);
            }
            free(frame->args);
            free(frame->name0);
            frame->args = NULL;
            frame->name0 = NULL;
            return false;
        }
        frame->count = index + 1;
    }
    return true;
}

bool wl_shell_set_args(struct wl_shell *sh, const char *name0,
                       char *const *args, size_t count) {
    struct wl_frame *frame = sh->frame;
    char *name_copy;

    if (frame == NULL) {
        return false;
    }
    /*
     * name0 may point into the frame being replaced (`set -- ...` passes the
     * current $0), so copy it before clearing.
     */
    name_copy = wl_duplicate_text(name0);
    if (name_copy == NULL) {
        return false;
    }
    wl_frame_clear(frame);
    frame->name0 = name_copy;
    if (count == 0) {
        return true;
    }
    frame->args = calloc(count, sizeof *frame->args);
    if (frame->args == NULL) {
        return false;
    }
    for (size_t index = 0; index < count; index++) {
        frame->args[index] = wl_duplicate_text(args[index]);
        if (frame->args[index] == NULL) {
            for (size_t done = 0; done < index; done++) {
                free(frame->args[done]);
            }
            free(frame->args);
            frame->args = NULL;
            frame->count = 0;
            return false;
        }
        frame->count = index + 1;
    }
    return true;
}

bool wl_shell_push_frame(struct wl_shell *sh, const char *name0,
                         char *const *args, size_t count) {
    struct wl_frame *frame = calloc(1, sizeof *frame);

    if (frame == NULL) {
        return false;
    }
    if (!wl_frame_fill(frame, name0, args, count)) {
        free(frame);
        return false;
    }
    frame->previous = sh->frame;
    sh->frame = frame;
    return true;
}

void wl_shell_pop_frame(struct wl_shell *sh) {
    struct wl_frame *frame = sh->frame;

    if (frame == NULL || frame->previous == NULL) {
        return;
    }
    sh->frame = frame->previous;
    wl_frame_clear(frame);
    free(frame);
}

void wl_shell_shift(struct wl_shell *sh, size_t count) {
    struct wl_frame *frame = sh->frame;

    if (frame == NULL || count == 0) {
        return;
    }
    if (count > frame->count) {
        count = frame->count;
    }
    for (size_t index = 0; index < count; index++) {
        free(frame->args[index]);
    }
    for (size_t index = count; index < frame->count; index++) {
        frame->args[index - count] = frame->args[index];
    }
    frame->count -= count;
    for (size_t index = frame->count; index < frame->count + count; index++) {
        frame->args[index] = NULL;
    }
}
