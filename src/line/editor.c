#include "line/editor.h"

#include "core/limits.h"
#include "jobs/signal.h"
#include "line/history.h"
#include "term/prompt.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

/*
 * Raw-mode editor.
 *
 * Deliberately small: no undo stack, no kill ring, no UTF-8 cursor movement.
 * Every escape sequence is decoded by hand; unknown sequences are ignored.
 * Redraw is the simple "carriage return, rewrite the line, erase the tail"
 * form, which is fast enough and survives resizes because it never depends on
 * the terminal width.
 */

#define WL_COMPLETION_MAX 200

struct wl_match {
    char *text; /* what would be inserted */
    bool directory;
};

struct wl_match_list {
    struct wl_match items[WL_COMPLETION_MAX];
    size_t count;
};

static void wl_write_all(int fd, const char *data, size_t length) {
    size_t written = 0;

    while (written < length) {
        ssize_t count = write(fd, data + written, length - written);

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        written += (size_t)count;
    }
}

static void wl_write_text(int fd, const char *text) {
    wl_write_all(fd, text, strlen(text));
}

static void wl_copy_text(char *destination, size_t capacity,
                         const char *source) {
    size_t length = strlen(source);

    if (length >= capacity) {
        length = capacity - 1;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static void wl_bell(struct wl_editor *editor) {
    wl_write_all(editor->write_fd, "\a", 1);
}

bool wl_editor_init(struct wl_editor *editor, int read_fd, int write_fd,
                    struct wl_history *history) {
    memset(editor, 0, sizeof *editor);
    editor->read_fd = read_fd;
    editor->write_fd = write_fd;
    editor->history = history;
    editor->capacity = WL_LINE_MAX;
    editor->buffer = malloc(editor->capacity);
    editor->saved_line = malloc(editor->capacity);
    editor->history_position = -1;
    if (editor->buffer == NULL || editor->saved_line == NULL) {
        free(editor->buffer);
        free(editor->saved_line);
        editor->buffer = NULL;
        editor->saved_line = NULL;
        return false;
    }
    editor->buffer[0] = '\0';
    editor->saved_line[0] = '\0';
    return true;
}

void wl_editor_free(struct wl_editor *editor) {
    free(editor->buffer);
    free(editor->saved_line);
    editor->buffer = NULL;
    editor->saved_line = NULL;
}

static bool wl_enter_raw(int fd, struct termios *saved) {
    struct termios raw;

    if (tcgetattr(fd, saved) != 0) {
        return false;
    }
    raw = *saved;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(tcflag_t)OPOST;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    return tcsetattr(fd, TCSADRAIN, &raw) == 0;
}

static void wl_leave_raw(int fd, const struct termios *saved) {
    (void)tcsetattr(fd, TCSADRAIN, saved);
}

static size_t wl_terminal_columns(int fd) {
    struct winsize size;
    const char *columns;
    long value;

    if (ioctl(fd, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return size.ws_col;
    }
    columns = getenv("COLUMNS");
    if (columns != NULL) {
        value = strtol(columns, NULL, 10);
        if (value > 0) {
            return (size_t)value;
        }
    }
    return 80;
}

static void wl_render(struct wl_editor *editor) {
    char sequence[32];
    size_t columns = wl_terminal_columns(editor->write_fd);
    size_t line_width = editor->prompt_width +
                        wl_prompt_visible_width_n(editor->buffer,
                                                  editor->length);
    size_t cursor_column = editor->prompt_width +
                           wl_prompt_visible_width_n(editor->buffer,
                                                     editor->cursor);
    bool show_right = editor->right_width > 0 &&
                      columns > editor->right_width + 1 &&
                      line_width + editor->right_width + 1 < columns;

    wl_write_all(editor->write_fd, "\r", 1);
    wl_write_text(editor->write_fd, editor->prompt);
    wl_write_all(editor->write_fd, editor->buffer, editor->length);
    wl_write_text(editor->write_fd, "\x1b[K");
    if (show_right) {
        size_t target = columns - editor->right_width;
        size_t move = target - line_width;

        if (move > 0) {
            (void)snprintf(sequence, sizeof sequence, "\x1b[%zuC", move);
            wl_write_text(editor->write_fd, sequence);
        }
        wl_write_text(editor->write_fd, editor->right_prompt);
    }
    wl_write_all(editor->write_fd, "\r", 1);
    if (cursor_column > 0) {
        (void)snprintf(sequence, sizeof sequence, "\x1b[%zuC", cursor_column);
        wl_write_text(editor->write_fd, sequence);
    }
}

static void wl_set_line(struct wl_editor *editor, const char *text) {
    size_t length = strlen(text);

    if (length >= editor->capacity) {
        length = editor->capacity - 1;
    }
    memcpy(editor->buffer, text, length);
    editor->buffer[length] = '\0';
    editor->length = length;
    editor->cursor = length;
}

static void wl_insert_byte(struct wl_editor *editor, char c) {
    if (editor->length + 1 >= editor->capacity) {
        wl_bell(editor);
        return;
    }
    memmove(editor->buffer + editor->cursor + 1, editor->buffer + editor->cursor,
            editor->length - editor->cursor);
    editor->buffer[editor->cursor] = c;
    editor->cursor++;
    editor->length++;
    editor->buffer[editor->length] = '\0';
}

static void wl_delete_before(struct wl_editor *editor) {
    if (editor->cursor == 0) {
        wl_bell(editor);
        return;
    }
    memmove(editor->buffer + editor->cursor - 1,
            editor->buffer + editor->cursor,
            editor->length - editor->cursor);
    editor->cursor--;
    editor->length--;
    editor->buffer[editor->length] = '\0';
}

static void wl_delete_at(struct wl_editor *editor) {
    if (editor->cursor >= editor->length) {
        wl_bell(editor);
        return;
    }
    memmove(editor->buffer + editor->cursor,
            editor->buffer + editor->cursor + 1,
            editor->length - editor->cursor - 1);
    editor->length--;
    editor->buffer[editor->length] = '\0';
}

static void wl_kill_to_end(struct wl_editor *editor) {
    editor->length = editor->cursor;
    editor->buffer[editor->length] = '\0';
}

static void wl_kill_to_start(struct wl_editor *editor) {
    memmove(editor->buffer, editor->buffer + editor->cursor,
            editor->length - editor->cursor);
    editor->length -= editor->cursor;
    editor->cursor = 0;
    editor->buffer[editor->length] = '\0';
}

static void wl_kill_word(struct wl_editor *editor) {
    size_t at = editor->cursor;

    while (at > 0 && isspace((unsigned char)editor->buffer[at - 1])) {
        at--;
    }
    while (at > 0 && !isspace((unsigned char)editor->buffer[at - 1])) {
        at--;
    }
    memmove(editor->buffer + at, editor->buffer + editor->cursor,
            editor->length - editor->cursor);
    editor->length -= editor->cursor - at;
    editor->cursor = at;
    editor->buffer[editor->length] = '\0';
}

static void wl_history_show(struct wl_editor *editor, long position) {
    if (editor->history == NULL) {
        return;
    }
    if (position < 0) {
        editor->history_position = -1;
        wl_set_line(editor, editor->saved_line);
        return;
    }
    if ((size_t)position < wl_history_count(editor->history)) {
        editor->history_position = position;
        wl_set_line(editor, wl_history_at(editor->history, (size_t)position));
    }
}

static void wl_history_move(struct wl_editor *editor, int direction) {
    size_t count;

    if (editor->history == NULL) {
        wl_bell(editor);
        return;
    }
    count = wl_history_count(editor->history);
    if (count == 0) {
        wl_bell(editor);
        return;
    }

    if (direction < 0) { /* older */
        if (editor->history_position == -1) {
            size_t length = editor->length;

            if (length >= editor->capacity) {
                length = editor->capacity - 1;
            }
            memcpy(editor->saved_line, editor->buffer, length);
            editor->saved_line[length] = '\0';
            wl_history_show(editor, (long)count - 1);
        } else if (editor->history_position > 0) {
            wl_history_show(editor, editor->history_position - 1);
        } else {
            wl_bell(editor);
        }
    } else { /* newer */
        if (editor->history_position == -1) {
            wl_bell(editor);
        } else if ((size_t)(editor->history_position + 1) < count) {
            wl_history_show(editor, editor->history_position + 1);
        } else {
            wl_history_show(editor, -1);
        }
    }
}

static bool wl_match_add(struct wl_match_list *list, const char *text,
                         bool directory) {
    for (size_t index = 0; index < list->count; index++) {
        if (strcmp(list->items[index].text, text) == 0) {
            return true;
        }
    }
    if (list->count >= WL_COMPLETION_MAX) {
        return true;
    }
    list->items[list->count].text = strdup(text);
    if (list->items[list->count].text == NULL) {
        return false;
    }
    list->items[list->count].directory = directory;
    list->count++;
    return true;
}

static void wl_match_free(struct wl_match_list *list) {
    for (size_t index = 0; index < list->count; index++) {
        free(list->items[index].text);
    }
    list->count = 0;
}

static bool wl_is_executable(const char *path) {
    struct stat info;

    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
        return false;
    }
    return access(path, X_OK) == 0;
}

static bool wl_is_directory(const char *path) {
    struct stat info;

    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static void wl_complete_commands(const char *prefix, size_t prefix_len,
                                 struct wl_match_list *matches) {
    const char *path = getenv("PATH");

    if (path == NULL) {
        return;
    }
    while (*path != '\0') {
        const char *colon = strchr(path, ':');
        size_t dir_len = (colon != NULL) ? (size_t)(colon - path)
                                         : strlen(path);
        char directory[WL_LINE_MAX];
        DIR *dir;
        struct dirent *entry;

        if (dir_len == 0) {
            (void)snprintf(directory, sizeof directory, ".");
        } else {
            (void)snprintf(directory, sizeof directory, "%.*s", (int)dir_len,
                           path);
        }
        dir = opendir(directory);
        if (dir != NULL) {
            while ((entry = readdir(dir)) != NULL) {
                char candidate[WL_LINE_MAX + 300];

                if (strncmp(entry->d_name, prefix, prefix_len) != 0) {
                    continue;
                }
                (void)snprintf(candidate, sizeof candidate, "%s/%s", directory,
                               entry->d_name);
                if (wl_is_executable(candidate)) {
                    (void)wl_match_add(matches, entry->d_name, false);
                }
            }
            (void)closedir(dir);
        }
        if (colon == NULL) {
            break;
        }
        path = colon + 1;
    }
}

static void wl_complete_paths(const char *prefix, size_t prefix_len,
                              struct wl_match_list *matches) {
    const char *slash = NULL;
    char directory[WL_LINE_MAX];
    const char *base;
    DIR *dir;
    struct dirent *entry;

    for (size_t index = 0; index < prefix_len; index++) {
        if (prefix[index] == '/') {
            slash = prefix + index;
        }
    }
    if (slash == NULL) {
        (void)snprintf(directory, sizeof directory, ".");
        base = prefix;
    } else {
        size_t dir_len = (size_t)(slash - prefix);

        if (dir_len == 0) {
            dir_len = 1; /* "/" */
        }
        (void)snprintf(directory, sizeof directory, "%.*s", (int)dir_len,
                       prefix);
        base = slash + 1;
    }

    dir = opendir(directory);
    if (dir == NULL) {
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        char full[WL_LINE_MAX];

        if (strncmp(entry->d_name, base, prefix_len - (size_t)(base - prefix)) !=
            0) {
            continue;
        }
        if (slash == NULL) {
            wl_copy_text(full, sizeof full, entry->d_name);
        } else {
            (void)snprintf(full, sizeof full, "%.*s%s", (int)(slash - prefix),
                           prefix, entry->d_name);
        }
        {
            char check[WL_LINE_MAX + 300];

            if (slash == NULL) {
                (void)snprintf(check, sizeof check, "%s/%s", directory,
                               entry->d_name);
            } else {
                wl_copy_text(check, sizeof check, full);
            }
            (void)wl_match_add(matches, full, wl_is_directory(check));
        }
    }
    (void)closedir(dir);
}

static void wl_complete(struct wl_editor *editor) {
    size_t start = editor->cursor;
    size_t prefix_len;
    bool first_word = true;
    struct wl_match_list matches;
    char prefix[WL_LINE_MAX];

    while (start > 0 && !isspace((unsigned char)editor->buffer[start - 1])) {
        start--;
    }
    prefix_len = editor->cursor - start;
    if (prefix_len >= sizeof prefix) {
        wl_bell(editor);
        return;
    }
    memcpy(prefix, editor->buffer + start, prefix_len);
    prefix[prefix_len] = '\0';

    for (size_t index = 0; index < start; index++) {
        if (!isspace((unsigned char)editor->buffer[index])) {
            first_word = false;
            break;
        }
    }

    memset(&matches, 0, sizeof matches);
    if (first_word) {
        wl_complete_commands(prefix, prefix_len, &matches);
    }
    wl_complete_paths(prefix, prefix_len, &matches);

    if (matches.count == 0) {
        wl_bell(editor);
        wl_match_free(&matches);
        return;
    }

    {
        char common[WL_LINE_MAX];
        size_t common_len;

        wl_copy_text(common, sizeof common, matches.items[0].text);
        common_len = strlen(common);
        for (size_t index = 1; index < matches.count; index++) {
            size_t at = 0;

            while (at < common_len && common[at] == matches.items[index].text[at]) {
                at++;
            }
            common_len = at;
            common[common_len] = '\0';
        }

        if (matches.count == 1) {
            bool directory = matches.items[0].directory;
            size_t new_len = strlen(matches.items[0].text) + 1;

            if (start + new_len + (editor->length - editor->cursor) + 1 >=
                editor->capacity) {
                wl_bell(editor);
            } else {
                memmove(editor->buffer + start + new_len,
                        editor->buffer + editor->cursor,
                        editor->length - editor->cursor);
                memcpy(editor->buffer + start, matches.items[0].text,
                       new_len - 1);
                editor->buffer[start + new_len - 1] = directory ? '/' : ' ';
                editor->length = start + new_len + (editor->length - editor->cursor);
                editor->cursor = start + new_len;
                editor->buffer[editor->length] = '\0';
            }
        } else if (common_len > prefix_len) {
            size_t suffix = editor->length - editor->cursor;
            size_t new_len = common_len;

            if (start + new_len + suffix + 1 >= editor->capacity) {
                wl_bell(editor);
            } else {
                memmove(editor->buffer + start + new_len,
                        editor->buffer + editor->cursor, suffix);
                memcpy(editor->buffer + start, common, new_len);
                editor->length = start + new_len + suffix;
                editor->cursor = start + new_len;
                editor->buffer[editor->length] = '\0';
            }
        } else {
            wl_bell(editor);
        }
    }

    wl_match_free(&matches);
}

static int wl_read_key(int fd) {
    unsigned char byte;

    for (;;) {
        ssize_t count = read(fd, &byte, 1);

        if (count == 1) {
            return byte;
        }
        if (count == 0) {
            return -1;
        }
        if (errno == EINTR) {
            if (wl_signal_interrupt_pending() || wl_signal_term_pending()) {
                return -2;
            }
            if (wl_signal_take_resize()) {
                return -3;
            }
            continue;
        }
        return -1;
    }
}

enum wl_edit_status wl_editor_read(struct wl_editor *editor,
                                   const char *prompt,
                                   const char *right_prompt, char *out,
                                   size_t capacity, size_t *length) {
    struct termios saved;
    enum wl_edit_status result = WL_EDIT_OK;
    bool raw;

    if (editor->buffer == NULL) {
        return WL_EDIT_ERROR;
    }
    (void)snprintf(editor->prompt, sizeof editor->prompt, "%s", prompt);
    editor->prompt_width = wl_prompt_visible_width(editor->prompt);
    (void)snprintf(editor->right_prompt, sizeof editor->right_prompt, "%s",
                   (right_prompt != NULL) ? right_prompt : "");
    editor->right_width = wl_prompt_visible_width(editor->right_prompt);
    editor->length = 0;
    editor->cursor = 0;
    editor->buffer[0] = '\0';
    editor->history_position = -1;
    editor->saved_line[0] = '\0';

    raw = wl_enter_raw(editor->read_fd, &saved);

    wl_render(editor);

    for (;;) {
        int key = wl_read_key(editor->read_fd);

        if (key == -2) {
            wl_write_text(editor->write_fd, "\r\n");
            result = WL_EDIT_INTERRUPTED;
            break;
        }
        if (key == -1) {
            wl_write_text(editor->write_fd, "\r\n");
            result = WL_EDIT_EOF;
            break;
        }
        if (key == -3) {
            wl_render(editor);
            continue;
        }

        switch (key) {
        case '\r':
        case '\n':
            if (editor->length >= capacity) {
                result = WL_EDIT_TOO_LONG;
            } else {
                memcpy(out, editor->buffer, editor->length);
                out[editor->length] = '\0';
                if (length != NULL) {
                    *length = editor->length;
                }
                if (editor->history != NULL) {
                    wl_history_add(editor->history, editor->buffer);
                }
                wl_write_text(editor->write_fd, "\r\n");
                result = WL_EDIT_OK;
            }
            goto done;

        case 3: /* Ctrl-C */
            wl_write_text(editor->write_fd, "^C\r\n");
            result = WL_EDIT_INTERRUPTED;
            goto done;

        case 4: /* Ctrl-D */
            if (editor->length == 0) {
                result = WL_EDIT_EOF;
                goto done;
            }
            wl_delete_at(editor);
            break;

        case 1:
            editor->cursor = 0;
            break;
        case 2:
            if (editor->cursor > 0) {
                editor->cursor--;
            }
            break;
        case 5:
            editor->cursor = editor->length;
            break;
        case 6:
            if (editor->cursor < editor->length) {
                editor->cursor++;
            }
            break;
        case 11:
            wl_kill_to_end(editor);
            break;
        case 21:
            wl_kill_to_start(editor);
            break;
        case 23:
            wl_kill_word(editor);
            break;
        case 12:
            wl_write_text(editor->write_fd, "\x1b[H\x1b[2J");
            break;
        case 16:
            wl_history_move(editor, -1);
            break;
        case 14:
            wl_history_move(editor, 1);
            break;
        case 9:
            wl_complete(editor);
            break;
        case 127:
        case 8:
            wl_delete_before(editor);
            break;
        case 27: { /* escape sequence */
            int second = wl_read_key(editor->read_fd);

            if (second == '[' || second == 'O') {
                int third = wl_read_key(editor->read_fd);

                if (third >= '0' && third <= '9') {
                    int fourth = wl_read_key(editor->read_fd);

                    if (third == '3' && fourth == '~') {
                        wl_delete_at(editor);
                    } else if ((third == '1' || third == '7') && fourth == '~') {
                        editor->cursor = 0;
                    } else if ((third == '4' || third == '8') && fourth == '~') {
                        editor->cursor = editor->length;
                    }
                } else {
                    switch (third) {
                    case 'A':
                        wl_history_move(editor, -1);
                        break;
                    case 'B':
                        wl_history_move(editor, 1);
                        break;
                    case 'C':
                        if (editor->cursor < editor->length) {
                            editor->cursor++;
                        }
                        break;
                    case 'D':
                        if (editor->cursor > 0) {
                            editor->cursor--;
                        }
                        break;
                    case 'H':
                        editor->cursor = 0;
                        break;
                    case 'F':
                        editor->cursor = editor->length;
                        break;
                    default:
                        break;
                    }
                }
            }
            break;
        }
        default:
            if (key >= 32 && key < 127) {
                wl_insert_byte(editor, (char)key);
            }
            break;
        }

        wl_render(editor);
    }

done:
    if (raw) {
        wl_leave_raw(editor->read_fd, &saved);
    }
    return result;
}
