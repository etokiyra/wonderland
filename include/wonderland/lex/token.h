#ifndef WL_LEX_TOKEN_H
#define WL_LEX_TOKEN_H

#include "ast/ast.h"

/*
 * Lexical tokens.
 *
 * A word token carries its parts (see ast/ast.h), not a raw string: quoting
 * and expansion structure are decided once, by the lexer, and never re-derived
 * from text. Reserved words are *not* token kinds; the parser classifies a
 * word by position, because `echo if` must work.
 */

enum wl_tok_kind {
    WL_TOK_EOF = 0,
    WL_TOK_NEWLINE,
    WL_TOK_WORD,
    WL_TOK_IO_NUMBER,

    WL_TOK_PIPE,
    WL_TOK_AND_IF,
    WL_TOK_OR_IF,
    WL_TOK_SEMI,
    WL_TOK_DSEMI,
    WL_TOK_AMP,
    WL_TOK_LPAREN,
    WL_TOK_RPAREN,

    WL_TOK_LESS,
    WL_TOK_GREAT,
    WL_TOK_DGREAT,
    WL_TOK_LESSGREAT,
    WL_TOK_CLOBBER,
    WL_TOK_DLESS,
    WL_TOK_DLESSDASH,
    WL_TOK_TLESS,
    WL_TOK_LESSAND,
    WL_TOK_GREATAND
};

struct wl_token {
    enum wl_tok_kind kind;
    struct wl_span span;
    struct wl_word *word; /* WL_TOK_WORD only */
    long io_number;       /* WL_TOK_IO_NUMBER only */
};

const char *wl_tok_name(enum wl_tok_kind kind);

#endif /* WL_LEX_TOKEN_H */
