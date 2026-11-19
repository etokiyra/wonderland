#ifndef WL_TERM_PROMPT_H
#define WL_TERM_PROMPT_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Prompt formatting.
 *
 * `wl_prompt_format` is the plain, legacy shape used by tests and by callers
 * that want no decoration: "wonderland:<cwd>$ ". The extended formatter adds
 * colour, Nerd Font icons, git branch, active jobs and the previous command's
 * duration. Both are pure: no I/O, no shell state. The caller writes the
 * result to the terminal (stderr), never to stdout.
 *
 * Colour escapes are emitted only when `style->color` is true. Icons are
 * UTF-8 Nerd Font private-use glyphs and are emitted only when
 * `style->icons` is true; otherwise the prompt uses ASCII words.
 *
 * The legacy formatter starts with "wonderland:"; the Starship formatter does
 * not. Interactive prompts are preceded by an OSC 133;A mark written by the
 * main loop, which terminal tests use as a stable boundary.
 */

struct wl_prompt_style {
    bool color;
    bool icons;
};

struct wl_prompt_info {
    int last_status;
    const char *cwd;
    const char *home;
    const char *git_branch; /* NULL or empty when not in a branch */
    bool repo_root;
    unsigned job_count;
    long duration_ms; /* < 0 before the first command */
    bool show_user;
    const char *user;
    const char *time_text; /* NULL = local HH:MM; tests inject a value */
};

/*
 * Starship-compatible stock prompt: a left prompt (duration, jobs, user,
 * character) and a right prompt (directory, branch, time) that the editor
 * aligns to the right edge. The glyphs, order and styles mirror the Caelestia
 * starship.toml: ◎/○ character, ◄ duration, ▶ jobs, ⌂ directory, △ repo root,
 * italic blue path, bright-blue branch, dimmed time.
 */
size_t wl_prompt_format_starship(char *left, size_t left_cap, char *right,
                                 size_t right_cap,
                                 const struct wl_prompt_info *info,
                                 const struct wl_prompt_style *style);

/* True when cwd itself is a git work-tree root (a .git dir or gitdir file). */
bool wl_prompt_is_repo_root(const char *cwd);

/* Current git branch for cwd, or a short detached hash. Returns false when
 * cwd is not inside a git work tree. Writes nothing on false. */
bool wl_prompt_git(const char *cwd, char *buf, size_t cap);

/* Visible terminal columns, ignoring SGR/CSI escapes and counting one column
 * per UTF-8 code point (the cursor and prompt use single-width glyphs). */
size_t wl_prompt_visible_width(const char *text);
size_t wl_prompt_visible_width_n(const char *text, size_t length);

size_t wl_prompt_format(char *buf, size_t cap, int last_status,
                        const char *cwd, const char *home);

size_t wl_prompt_format_ex(char *buf, size_t cap, int last_status,
                           const char *cwd, const char *home,
                           const char *git_branch, unsigned job_count,
                           long duration_ms,
                           const struct wl_prompt_style *style);

#endif /* WL_TERM_PROMPT_H */
