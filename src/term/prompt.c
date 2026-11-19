#include "term/prompt.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/*
 * Prompt formatting. Pure: the caller supplies cwd and HOME, and decides where
 * the text is written (stderr, never stdout). The only filesystem read is
 * `wl_prompt_git`, which looks for .git/HEAD while walking up from cwd; it
 * never runs a command.
 */

#define WL_PROMPT_PATH_MAX 48
#define WL_PROMPT_BUFFER 512
#define WL_PROMPT_BRANCH_MAX 40
#define WL_PROMPT_GIT_PATH 1024
#define WL_PROMPT_ICON_FOLDER "\xef\x81\xbb"
#define WL_PROMPT_ICON_BRANCH "\xee\x9c\xa5"
#define WL_PROMPT_ICON_CHECK "\xef\x80\x8c"
#define WL_PROMPT_ICON_CROSS "\xef\x80\x8d"
#define WL_PROMPT_ICON_JOBS "\xef\x80\x93"
#define WL_PROMPT_ICON_CLOCK "\xef\x80\x97"
#define WL_PROMPT_ICON_ARROW "\xef\x84\x85"

struct wl_prompt_builder {
    char *buf;
    size_t cap;
    size_t len;
};

static void wl_pb_text(struct wl_prompt_builder *builder, const char *text,
                       size_t length) {
    if (builder->buf != NULL && builder->cap > 0 &&
        builder->len < builder->cap) {
        size_t room = builder->cap - builder->len - 1;
        size_t copy = (length < room) ? length : room;

        memcpy(builder->buf + builder->len, text, copy);
        builder->buf[builder->len + copy] = '\0';
    }
    builder->len += length;
}

static void wl_pb_str(struct wl_prompt_builder *builder, const char *text) {
    wl_pb_text(builder, text, strlen(text));
}

static void wl_prompt_path(char *out, size_t cap, const char *cwd,
                           const char *home) {
    size_t home_len = (home != NULL) ? strlen(home) : 0;

    if (home_len > 0 && strncmp(cwd, home, home_len) == 0 &&
        (cwd[home_len] == '\0' || cwd[home_len] == '/')) {
        (void)snprintf(out, cap, "~%s", cwd + home_len);
    } else {
        (void)snprintf(out, cap, "%s", cwd);
    }

    if (strlen(out) <= WL_PROMPT_PATH_MAX) {
        return;
    }

    /* Too long for a prompt: keep the leaf, which is what people read. */
    {
        const char *slash = strrchr(cwd, '/');

        if (slash != NULL && slash[1] != '\0') {
            (void)snprintf(out, cap, "../%s", slash + 1);
        }
    }
}

static bool wl_prompt_copy_branch(const char *text, char *buf, size_t cap) {
    size_t length = 0;

    if (text == NULL || text[0] == '\0') {
        return false;
    }
    while (text[length] != '\0' && text[length] != '\n' &&
           text[length] != '\r' && length + 1 < cap) {
        unsigned char c = (unsigned char)text[length];

        buf[length] = (c >= 32 && c < 127) ? (char)c : '?';
        length++;
    }
    buf[length] = '\0';
    return length > 0;
}

static bool wl_prompt_join(char *out, size_t cap, const char *base,
                           const char *name) {
    size_t base_len = strlen(base);
    size_t name_len = strlen(name);
    bool slash = !(base_len > 0 && base[base_len - 1] == '/');

    if (base_len + (slash ? 1u : 0u) + name_len + 1 > cap) {
        return false;
    }
    memcpy(out, base, base_len);
    if (slash) {
        out[base_len++] = '/';
    }
    memcpy(out + base_len, name, name_len + 1);
    return true;
}

static bool wl_prompt_read_head(const char *gitdir, char *buf, size_t cap) {
    char path[WL_PROMPT_GIT_PATH];
    char line[256];
    FILE *file;

    (void)snprintf(path, sizeof path, "%s/HEAD", gitdir);
    file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    if (fgets(line, sizeof line, file) == NULL) {
        (void)fclose(file);
        return false;
    }
    (void)fclose(file);

    if (strncmp(line, "ref: refs/heads/", 16) == 0) {
        return wl_prompt_copy_branch(line + 16, buf, cap);
    }
    /* Detached HEAD: show a short hash. */
    {
        char short_hash[16];
        size_t length = 0;

        while (length < sizeof short_hash - 1 && line[length] != '\0' &&
               line[length] != '\n' && line[length] != '\r') {
            short_hash[length] = line[length];
            length++;
        }
        if (length < 7) {
            return false;
        }
        if (length > 8) {
            length = 8;
        }
        short_hash[length] = '\0';
        return wl_prompt_copy_branch(short_hash, buf, cap);
    }
}

bool wl_prompt_git(const char *cwd, char *buf, size_t cap) {
    char dir[WL_PROMPT_GIT_PATH];
    int depth;

    if (cwd == NULL || cwd[0] != '/' || buf == NULL || cap == 0) {
        return false;
    }
    if (strlen(cwd) >= sizeof dir) {
        return false;
    }
    memcpy(dir, cwd, strlen(cwd) + 1);

    for (depth = 0; depth < 64; depth++) {
        char dot_git[WL_PROMPT_GIT_PATH];
        struct stat info;

        if (!wl_prompt_join(dot_git, sizeof dot_git, dir, ".git")) {
            break;
        }
        if (stat(dot_git, &info) == 0) {
            if (S_ISDIR(info.st_mode)) {
                if (wl_prompt_read_head(dot_git, buf, cap)) {
                    return true;
                }
            } else if (S_ISREG(info.st_mode)) {
                FILE *file = fopen(dot_git, "r");
                char line[WL_PROMPT_GIT_PATH];

                if (file != NULL) {
                    if (fgets(line, sizeof line, file) != NULL &&
                        strncmp(line, "gitdir: ", 8) == 0) {
                        char target[WL_PROMPT_GIT_PATH];
                        char *newline = strpbrk(line + 8, "\r\n");

                        if (newline != NULL) {
                            *newline = '\0';
                        }
                        bool joined;

                        if (line[8] == '/') {
                            joined = strlen(line + 8) < sizeof target;
                            if (joined) {
                                memcpy(target, line + 8,
                                       strlen(line + 8) + 1);
                            }
                        } else {
                            joined = wl_prompt_join(target, sizeof target, dir,
                                                    line + 8);
                        }
                        (void)fclose(file);
                        if (joined && wl_prompt_read_head(target, buf, cap)) {
                            return true;
                        }
                        file = NULL;
                    }
                    if (file != NULL) {
                        (void)fclose(file);
                    }
                }
            }
        }

        if (strcmp(dir, "/") == 0) {
            break;
        }
        {
            char *slash = strrchr(dir, '/');

            if (slash == NULL) {
                break;
            }
            if (slash == dir) {
                dir[1] = '\0';
            } else {
                *slash = '\0';
            }
        }
    }
    return false;
}

size_t wl_prompt_visible_width_n(const char *text, size_t length) {
    size_t width = 0;
    size_t index = 0;

    if (text == NULL) {
        return 0;
    }
    while (index < length) {
        unsigned char c = (unsigned char)text[index];

        if (c == 0x1b) {
            index++;
            if (index < length && text[index] == '[') {
                index++;
                while (index < length &&
                       !(text[index] >= 0x40 && text[index] <= 0x7e)) {
                    index++;
                }
                if (index < length) {
                    index++;
                }
            } else if (index < length) {
                index++;
            }
            continue;
        }
        if (c == '\n' || c == '\r') {
            width = 0;
            index++;
            continue;
        }
        if (c < 0x80) {
            width++;
            index++;
            continue;
        }
        /* Count one column per code point; all prompt icons are single-width. */
        if ((c & 0xe0) == 0xc0) {
            index += 2;
        } else if ((c & 0xf0) == 0xe0) {
            index += 3;
        } else if ((c & 0xf8) == 0xf0) {
            index += 4;
        } else {
            index += 1;
        }
        if (index > length) {
            index = length;
        }
        width++;
    }
    return width;
}

size_t wl_prompt_visible_width(const char *text) {
    return (text != NULL) ? wl_prompt_visible_width_n(text, strlen(text)) : 0;
}

static void wl_prompt_duration(char *buf, size_t cap, long duration_ms) {
    if (duration_ms < 60000) {
        (void)snprintf(buf, cap, "%.2fs", (double)duration_ms / 1000.0);
    } else {
        (void)snprintf(buf, cap, "%ldm%02lds", duration_ms / 60000,
                       (duration_ms / 1000) % 60);
    }
}

/* Starship/humantime-ish duration: 42ms, 1.2s, 1m 03s. */
static void wl_starship_duration(char *buf, size_t cap, long duration_ms) {
    if (duration_ms < 1000) {
        (void)snprintf(buf, cap, "%ldms", duration_ms);
    } else if (duration_ms < 60000) {
        long seconds = duration_ms / 1000;
        long tenths = (duration_ms % 1000) / 100;

        if (tenths == 0) {
            (void)snprintf(buf, cap, "%lds", seconds);
        } else {
            (void)snprintf(buf, cap, "%ld.%lds", seconds, tenths);
        }
    } else {
        (void)snprintf(buf, cap, "%ldm %02lds", duration_ms / 60000,
                       (duration_ms / 1000) % 60);
    }
}

static void wl_pb_sgr(struct wl_prompt_builder *builder,
                      const struct wl_prompt_style *style, const char *code) {
    if (style->color) {
        wl_pb_str(builder, code);
    }
}

static void wl_pb_reset(struct wl_prompt_builder *builder,
                        const struct wl_prompt_style *style) {
    if (style->color) {
        wl_pb_str(builder, "\x1b[0m");
    }
}

/* Points at the start of the last `count` slash-separated components. */
static const char *wl_last_components(const char *path, unsigned count) {
    const char *cursor = path + strlen(path);
    unsigned seen = 0;

    while (cursor > path) {
        cursor--;
        if (*cursor == '/' && cursor[1] != '\0') {
            seen++;
            if (seen == count) {
                return cursor + 1;
            }
        }
    }
    return path;
}

static void wl_starship_path(char *out, size_t cap, const char *cwd,
                             const char *home, bool icons) {
    size_t home_len = (home != NULL) ? strlen(home) : 0;
    const char *home_symbol = icons ? "\xe2\x8c\x82" : "~";
    const char *ellipsis = icons ? "\xe2\x80\xa6" : "...";

    if (cwd == NULL || cwd[0] == '\0') {
        (void)snprintf(out, cap, "%s", home_symbol);
        return;
    }
    if (home_len > 0 && strncmp(cwd, home, home_len) == 0 &&
        (cwd[home_len] == '\0' || cwd[home_len] == '/')) {
        const char *rest = cwd + home_len;
        const char *tail;

        if (*rest == '/') {
            rest++;
        }
        if (*rest == '\0') {
            (void)snprintf(out, cap, "%s", home_symbol);
            return;
        }
        tail = wl_last_components(rest, 2);
        if (tail == rest) {
            (void)snprintf(out, cap, "%s/%s", home_symbol, rest);
        } else {
            (void)snprintf(out, cap, "%s/%s", ellipsis, tail);
        }
        return;
    }

    {
        const char *tail = wl_last_components(cwd, 2);

        if (tail == cwd || tail == cwd + 1) {
            (void)snprintf(out, cap, "%s", cwd);
        } else {
            (void)snprintf(out, cap, "%s/%s", ellipsis, tail);
        }
    }
}

static void wl_starship_branch(char *out, size_t cap, const char *branch,
                               bool icons) {
    size_t length;

    if (branch == NULL || branch[0] == '\0' || strcmp(branch, "main") == 0 ||
        strcmp(branch, "master") == 0) {
        out[0] = '\0';
        return;
    }
    length = strlen(branch);
    if (length > 11) {
        (void)snprintf(out, cap, "%.11s%s", branch, icons ? "\xe2\x8b\xaf" : "...");
    } else {
        (void)snprintf(out, cap, "%s", branch);
    }
}

bool wl_prompt_is_repo_root(const char *cwd) {
    char dot_git[WL_PROMPT_GIT_PATH];
    struct stat info;

    if (cwd == NULL || cwd[0] != '/') {
        return false;
    }
    if (!wl_prompt_join(dot_git, sizeof dot_git, cwd, ".git")) {
        return false;
    }
    return stat(dot_git, &info) == 0 &&
           (S_ISDIR(info.st_mode) || S_ISREG(info.st_mode));
}

size_t wl_prompt_format_starship(char *left, size_t left_cap, char *right,
                                 size_t right_cap,
                                 const struct wl_prompt_info *info,
                                 const struct wl_prompt_style *style) {
    struct wl_prompt_builder left_builder;
    struct wl_prompt_builder right_builder;
    struct wl_prompt_style plain = {false, false};
    char text[WL_PROMPT_BUFFER];
    char path[WL_PROMPT_BUFFER];
    bool read_only;

    if (style == NULL) {
        style = &plain;
    }
    if (info == NULL) {
        return 0;
    }
    left_builder.buf = left;
    left_builder.cap = left_cap;
    left_builder.len = 0;
    right_builder.buf = right;
    right_builder.cap = right_cap;
    right_builder.len = 0;
    if (left_cap > 0) {
        left[0] = '\0';
    }
    if (right_cap > 0) {
        right[0] = '\0';
    }

    /* Left: duration, jobs, user, character. */
    if (info->duration_ms >= 0) {
        wl_starship_duration(text, sizeof text, info->duration_ms);
        wl_pb_sgr(&left_builder, style, "\x1b[3;37m");
        if (style->icons) {
            wl_pb_str(&left_builder, "\xe2\x97\x84 ");
        }
        wl_pb_str(&left_builder, text);
        wl_pb_str(&left_builder, " ");
        wl_pb_reset(&left_builder, style);
    }
    if (info->job_count > 0) {
        (void)snprintf(text, sizeof text, "%u", info->job_count);
        if (style->icons) {
            wl_pb_sgr(&left_builder, style, "\x1b[3;34m");
            wl_pb_str(&left_builder, "\xe2\x96\xb6");
            wl_pb_reset(&left_builder, style);
            wl_pb_sgr(&left_builder, style, "\x1b[37m");
            wl_pb_str(&left_builder, text);
            wl_pb_reset(&left_builder, style);
            wl_pb_str(&left_builder, " ");
        } else {
            wl_pb_str(&left_builder, "[");
            wl_pb_str(&left_builder, text);
            wl_pb_str(&left_builder, "] ");
        }
    }
    if (info->show_user && info->user != NULL && info->user[0] != '\0') {
        wl_pb_sgr(&left_builder, style, "\x1b[1;3;93m");
        if (style->icons) {
            wl_pb_str(&left_builder, "\xe2\xad\x98 ");
        }
        wl_pb_str(&left_builder, info->user);
        wl_pb_reset(&left_builder, style);
        wl_pb_str(&left_builder, " ");
    }
    if (info->last_status != 0) {
        wl_pb_sgr(&left_builder, style, "\x1b[3;35m");
        wl_pb_str(&left_builder, style->icons ? "\xe2\x97\x8b" : "!");
    } else {
        wl_pb_sgr(&left_builder, style, "\x1b[1;3;93m");
        wl_pb_str(&left_builder, style->icons ? "\xe2\x97\x8e" : ">");
    }
    wl_pb_reset(&left_builder, style);
    wl_pb_str(&left_builder, " ");

    /* Right: directory, branch, time. */
    wl_starship_path(path, sizeof path, info->cwd, info->home, style->icons);
    read_only = (info->cwd != NULL) && access(info->cwd, W_OK) != 0;
    wl_pb_sgr(&right_builder, style,
              info->repo_root ? "\x1b[1;34m" : "\x1b[3;34m");
    wl_pb_str(&right_builder, path);
    wl_pb_reset(&right_builder, style);
    if (read_only) {
        wl_pb_sgr(&right_builder, style, "\x1b[31m");
        wl_pb_str(&right_builder, style->icons ? " \xe2\x97\x88" : " (ro)");
        wl_pb_reset(&right_builder, style);
    }
    if (info->repo_root) {
        wl_pb_sgr(&right_builder, style, "\x1b[1;94m");
        wl_pb_str(&right_builder, style->icons ? " \xe2\x96\xb3" : " *");
        wl_pb_reset(&right_builder, style);
    }
    if (info->git_branch != NULL && info->git_branch[0] != '\0') {
        wl_starship_branch(text, sizeof text, info->git_branch, style->icons);
        if (text[0] != '\0') {
            if (style->icons) {
                wl_pb_str(&right_builder, " ");
                wl_pb_sgr(&right_builder, style, "\x1b[3;94m");
                wl_pb_str(&right_builder, text);
            } else {
                wl_pb_sgr(&right_builder, style, "\x1b[3;94m");
                wl_pb_str(&right_builder, " (");
                wl_pb_str(&right_builder, text);
                wl_pb_str(&right_builder, ")");
            }
            wl_pb_reset(&right_builder, style);
        }
    }
    if (info->time_text != NULL) {
        (void)snprintf(text, sizeof text, "%s", info->time_text);
    } else {
        time_t now = time(NULL);
        struct tm local;

        text[0] = '\0';
        if (now != (time_t)-1 && localtime_r(&now, &local) != NULL) {
            (void)strftime(text, sizeof text, "%H:%M", &local);
        }
    }
    if (text[0] != '\0') {
        wl_pb_sgr(&right_builder, style, "\x1b[2;3;37m");
        wl_pb_str(&right_builder, " ");
        wl_pb_str(&right_builder, text);
        wl_pb_reset(&right_builder, style);
    }

    if (left_cap > 0) {
        size_t end = (left_builder.len < left_cap) ? left_builder.len
                                                   : left_cap - 1;
        left[end] = '\0';
    }
    if (right_cap > 0) {
        size_t end = (right_builder.len < right_cap) ? right_builder.len
                                                     : right_cap - 1;
        right[end] = '\0';
    }
    return left_builder.len;
}

size_t wl_prompt_format_ex(char *buf, size_t cap, int last_status,
                           const char *cwd, const char *home,
                           const char *git_branch, unsigned job_count,
                           long duration_ms,
                           const struct wl_prompt_style *style) {
    struct wl_prompt_builder builder;
    struct wl_prompt_style plain = {false, false};
    char path[WL_PROMPT_BUFFER];
    char number[WL_PROMPT_BRANCH_MAX];

    if (style == NULL) {
        style = &plain;
    }
    builder.buf = buf;
    builder.cap = cap;
    builder.len = 0;
    if (cap > 0) {
        buf[0] = '\0';
    }
    wl_prompt_path(path, sizeof path, (cwd != NULL) ? cwd : "?", home);

    wl_pb_str(&builder, "wonderland:");
    if (style->icons) {
        if (style->color) {
            wl_pb_str(&builder, "\x1b[1;36m");
        }
        wl_pb_str(&builder, " " WL_PROMPT_ICON_FOLDER " ");
        wl_pb_str(&builder, path);
    } else if (style->color) {
        wl_pb_str(&builder, "\x1b[1;36m ");
        wl_pb_str(&builder, path);
    } else {
        wl_pb_str(&builder, path);
    }
    if (style->color) {
        wl_pb_str(&builder, "\x1b[0m");
    }

    if (git_branch != NULL && git_branch[0] != '\0') {
        if (style->color) {
            wl_pb_str(&builder, "\x1b[35m");
        }
        if (style->icons) {
            wl_pb_str(&builder, " " WL_PROMPT_ICON_BRANCH " ");
        } else {
            wl_pb_str(&builder, " (");
        }
        wl_pb_str(&builder, git_branch);
        if (!style->icons) {
            wl_pb_str(&builder, ")");
        }
        if (style->color) {
            wl_pb_str(&builder, "\x1b[0m");
        }
    }

    if (job_count > 0) {
        (void)snprintf(number, sizeof number, "%u", job_count);
        if (style->color) {
            wl_pb_str(&builder, "\x1b[33m");
        }
        if (style->icons) {
            wl_pb_str(&builder, " " WL_PROMPT_ICON_JOBS " ");
        } else {
            wl_pb_str(&builder, " [");
        }
        wl_pb_str(&builder, number);
        if (!style->icons) {
            wl_pb_str(&builder, " jobs]");
        }
        if (style->color) {
            wl_pb_str(&builder, "\x1b[0m");
        }
    }

    if (duration_ms >= 100) {
        wl_prompt_duration(number, sizeof number, duration_ms);
        if (style->color) {
            wl_pb_str(&builder, "\x1b[2;33m");
        }
        if (style->icons) {
            wl_pb_str(&builder, " " WL_PROMPT_ICON_CLOCK " ");
        } else {
            wl_pb_str(&builder, " ");
        }
        wl_pb_str(&builder, number);
        if (style->color) {
            wl_pb_str(&builder, "\x1b[0m");
        }
    }

    if (last_status != 0) {
        (void)snprintf(number, sizeof number, "%d", last_status);
        if (style->color) {
            wl_pb_str(&builder, "\x1b[1;31m");
        }
        if (style->icons) {
            wl_pb_str(&builder, " " WL_PROMPT_ICON_CROSS " ");
        } else {
            wl_pb_str(&builder, " [");
        }
        wl_pb_str(&builder, number);
        if (!style->icons) {
            wl_pb_str(&builder, "]");
        }
        if (style->color) {
            wl_pb_str(&builder, "\x1b[0m");
        }
    } else if (style->icons) {
        if (style->color) {
            wl_pb_str(&builder, "\x1b[32m");
        }
        wl_pb_str(&builder, " " WL_PROMPT_ICON_CHECK);
        if (style->color) {
            wl_pb_str(&builder, "\x1b[0m");
        }
    }

    if (style->color) {
        wl_pb_str(&builder, (last_status != 0) ? "\x1b[1;31m" : "\x1b[1;32m");
    }
    if (style->icons) {
        wl_pb_str(&builder, " " WL_PROMPT_ICON_ARROW " ");
    } else {
        wl_pb_str(&builder, " $ ");
    }
    if (style->color) {
        wl_pb_str(&builder, "\x1b[0m");
    }

    if (cap > 0) {
        size_t end = (builder.len < cap) ? builder.len : cap - 1;

        buf[end] = '\0';
    }
    return builder.len;
}

size_t wl_prompt_format(char *buf, size_t cap, int last_status,
                        const char *cwd, const char *home) {
    return wl_prompt_format_ex(buf, cap, last_status, cwd, home, NULL, 0, 0,
                               NULL);
}
