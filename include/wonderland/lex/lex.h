#ifndef WL_LEX_LEX_H
#define WL_LEX_LEX_H

#include "ast/ast.h"
#include "core/diag.h"
#include "lex/token.h"

/*
 * Lexer: bytes to tokens and words.
 *
 * Pull-based and lazy: wl_lex_next() consumes exactly the bytes of the token
 * it returns. That matters for here-documents, whose bodies must be read from
 * the position just after the newline that ends the command line, before the
 * next token is scanned (see wl_lex_read_heredoc).
 *
 * Positions are byte offsets. Line/column are computed only when a diagnostic
 * is printed (core/diag.h), so a sub-parse of a command substitution does not
 * need to reconstruct a line map.
 */

#define WL_LEX_MAX_DEPTH 64

struct wl_lexer {
    const char *src;
    size_t len;
    size_t pos;
    struct wl_arena *arena;
    struct wl_diag *diag;
    int depth;
};

void wl_lex_init(struct wl_lexer *lexer, const char *src, size_t len,
                 struct wl_arena *arena, struct wl_diag *diag);

/* Returns false only on a lexical error (diagnostic set). EOF is a token. */
bool wl_lex_next(struct wl_lexer *lexer, struct wl_token *token);

/* Scans one word at the current position and stores it in *word. */
bool wl_lex_word(struct wl_lexer *lexer, struct wl_word **word);

/*
 * Reads one here-document body. The lexer must be positioned immediately
 * after the newline that ends the command line. On success, advances past the
 * delimiter line. Unterminated input sets diag->incomplete and returns false.
 */
bool wl_lex_read_heredoc(struct wl_lexer *lexer, const struct wl_str *delimiter,
                         bool strip_tabs, bool expand,
                         struct wl_heredoc *heredoc);

/* Byte access used by the word scanner. */
char wl_lex_peek_at(const struct wl_lexer *lexer, size_t offset);
void wl_lex_skip(struct wl_lexer *lexer, size_t count);
bool wl_lex_done(const struct wl_lexer *lexer);

#endif /* WL_LEX_LEX_H */
