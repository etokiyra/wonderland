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
 * The prompt always starts with the literal bytes "wonderland:" so that
 * terminal tests and users can find it without parsing escapes.
 */

struct wl_prompt_style {
    bool color;
    bool icons;
};

/* Current git branch for cwd, or a short detached hash. Returns false when
 * cwd is not inside a git work tree. Writes nothing on false. */
bool wl_prompt_git(const char *cwd, char *buf, size_t cap);

/* Visible terminal columns, ignoring SGR/CSI escapes and counting one column
 * per UTF-8 code point (the cursor and prompt use single-width glyphs). */
size_t wl_prompt_visible_width(const char *text);

size_t wl_prompt_format(char *buf, size_t cap, int last_status,
                        const char *cwd, const char *home);

size_t wl_prompt_format_ex(char *buf, size_t cap, int last_status,
                           const char *cwd, const char *home,
                           const char *git_branch, unsigned job_count,
                           long duration_ms,
                           const struct wl_prompt_style *style);

#endif /* WL_TERM_PROMPT_H */
