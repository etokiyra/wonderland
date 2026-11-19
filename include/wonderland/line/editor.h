#ifndef WL_LINE_EDITOR_H
#define WL_LINE_EDITOR_H

#include <stdbool.h>
#include <stddef.h>

struct wl_history;

/*
 * Raw-mode line editor.
 *
 * One line at a time: cursor movement, insertion/deletion, history recall,
 * basic tab completion, Ctrl-A/E/K/U/W/L/C/D, arrow keys, Home/End/Delete.
 * The terminal is put in raw mode only while a line is being edited, so
 * commands run with the canonical settings they expect.
 *
 * The editor writes its prompt and redraws to write_fd (normally stderr) and
 * reads keys from read_fd (normally stdin). Prompt width skips ANSI escapes
 * and counts UTF-8 code points; insertion and cursor movement are still
 * byte-oriented, so UTF-8 editing remains limited.
 */

struct wl_editor {
    int read_fd;
    int write_fd;
    struct wl_history *history;

    char *buffer;
    size_t capacity;
    size_t length;
    size_t cursor;

    char prompt[512];
    size_t prompt_width; /* visible columns, not bytes (escapes/icons) */

    char right_prompt[512];
    size_t right_width;

    long history_position; /* -1 = editing the current line */
    char *saved_line;
};

enum wl_edit_status {
    WL_EDIT_OK = 0,
    WL_EDIT_EOF,
    WL_EDIT_INTERRUPTED,
    WL_EDIT_TOO_LONG,
    WL_EDIT_ERROR
};

bool wl_editor_init(struct wl_editor *editor, int read_fd, int write_fd,
                    struct wl_history *history);
void wl_editor_free(struct wl_editor *editor);

/*
 * Reads one line (without the newline) into out. Returns WL_EDIT_EOF on Ctrl-D
 * at an empty line, WL_EDIT_INTERRUPTED on Ctrl-C.
 */
enum wl_edit_status wl_editor_read(struct wl_editor *editor,
                                   const char *prompt,
                                   const char *right_prompt, char *out,
                                   size_t capacity, size_t *length);

#endif /* WL_LINE_EDITOR_H */
