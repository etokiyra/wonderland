#ifndef WL_EXPAND_EXPAND_H
#define WL_EXPAND_EXPAND_H

#include <stdbool.h>
#include <stddef.h>

#include "ast/ast.h"
#include "core/buf.h"

/*
 * Expansion: a parsed word becomes zero or more argument fields.
 *
 * Order (POSIX): tilde/parameter/command/arithmetic expansion, then field
 * splitting on the bytes that came from *unquoted* expansions, then pathname
 * expansion, then quote removal. Quoting information comes from the lexer's
 * word parts; nothing is re-parsed from text.
 *
 * Command substitution is injected as a callback so this module never depends
 * on execution (docs/ARCHITECTURE.md §3): the executor owns fork/exec.
 */

struct wl_shell;

struct wl_argv {
    char **items;
    size_t count;
    size_t capacity;
};

void wl_argv_init(struct wl_argv *argv);
void wl_argv_free(struct wl_argv *argv);

/* Takes ownership of a malloc'd string. */
bool wl_argv_push(struct wl_argv *argv, char *owned);

/*
 * Runs program in a subshell and returns its standard output in `output`.
 * Implemented by the executor. `status` receives the subshell's exit status.
 */
typedef bool (*wl_capture_fn)(void *context, const struct wl_node *program,
                              struct wl_buf *output, int *status);

struct wl_expand_ctx {
    struct wl_shell *shell;
    wl_capture_fn capture;
    void *capture_context;

    /* Filled when an expansion fails; the executor reports and sets status. */
    char error[256];
};

/* Expands each word, appending its fields to out. */
bool wl_expand_words(struct wl_expand_ctx *ctx, struct wl_word *const *words,
                     size_t count, struct wl_argv *out);

/* Expands one word without splitting or globbing (assignment values). */
bool wl_expand_single(struct wl_expand_ctx *ctx, const struct wl_word *word,
                      struct wl_buf *out);

/*
 * Expands one word and requires exactly one field (redirection target).
 * Returns false on expansion error. Sets *ambiguous when the word produced
 * zero or several fields, in which case *out is untouched.
 */
bool wl_expand_target(struct wl_expand_ctx *ctx, const struct wl_word *word,
                      char **out, bool *ambiguous);

#endif /* WL_EXPAND_EXPAND_H */
