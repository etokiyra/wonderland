#include "lex/lex.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "ast/arena.h"
#include "core/buf.h"

char wl_lex_peek_at(const struct wl_lexer *lexer, size_t offset) {
    if (lexer->pos + offset >= lexer->len) {
        return '\0';
    }
    return lexer->src[lexer->pos + offset];
}

void wl_lex_skip(struct wl_lexer *lexer, size_t count) {
    if (count > lexer->len - lexer->pos) {
        lexer->pos = lexer->len;
        return;
    }
    lexer->pos += count;
}

bool wl_lex_done(const struct wl_lexer *lexer) {
    return lexer->pos >= lexer->len;
}

void wl_lex_init(struct wl_lexer *lexer, const char *src, size_t len,
                 struct wl_arena *arena, struct wl_diag *diag) {
    lexer->src = src;
    lexer->len = len;
    lexer->pos = 0;
    lexer->arena = arena;
    lexer->diag = diag;
    lexer->depth = 0;
}

const char *wl_tok_name(enum wl_tok_kind kind) {
    switch (kind) {
    case WL_TOK_EOF:
        return "EOF";
    case WL_TOK_NEWLINE:
        return "NEWLINE";
    case WL_TOK_WORD:
        return "WORD";
    case WL_TOK_IO_NUMBER:
        return "IO_NUMBER";
    case WL_TOK_PIPE:
        return "|";
    case WL_TOK_AND_IF:
        return "&&";
    case WL_TOK_OR_IF:
        return "||";
    case WL_TOK_SEMI:
        return ";";
    case WL_TOK_DSEMI:
        return ";;";
    case WL_TOK_AMP:
        return "&";
    case WL_TOK_LPAREN:
        return "(";
    case WL_TOK_RPAREN:
        return ")";
    case WL_TOK_LESS:
        return "<";
    case WL_TOK_GREAT:
        return ">";
    case WL_TOK_DGREAT:
        return ">>";
    case WL_TOK_LESSGREAT:
        return "<>";
    case WL_TOK_CLOBBER:
        return ">|";
    case WL_TOK_DLESS:
        return "<<";
    case WL_TOK_DLESSDASH:
        return "<<-";
    case WL_TOK_TLESS:
        return "<<<";
    case WL_TOK_LESSAND:
        return "<&";
    case WL_TOK_GREATAND:
        return ">&";
    default:
        return "?";
    }
}

static enum wl_tok_kind wl_match_operator(const char *text, size_t available,
                                          size_t *length) {
    struct wl_operator {
        const char *text;
        enum wl_tok_kind kind;
    };
    static const struct wl_operator operators[] = {
        {"<<<", WL_TOK_TLESS},   {"<<-", WL_TOK_DLESSDASH},
        {"<<", WL_TOK_DLESS},    {"<&", WL_TOK_LESSAND},
        {"<>", WL_TOK_LESSGREAT}, {"<", WL_TOK_LESS},
        {">>", WL_TOK_DGREAT},   {">|", WL_TOK_CLOBBER},
        {">&", WL_TOK_GREATAND}, {">", WL_TOK_GREAT},
        {"&&", WL_TOK_AND_IF},   {"||", WL_TOK_OR_IF},
        {";;", WL_TOK_DSEMI},    {";", WL_TOK_SEMI},
        {"|", WL_TOK_PIPE},      {"&", WL_TOK_AMP},
        {"(", WL_TOK_LPAREN},    {")", WL_TOK_RPAREN},
    };

    for (size_t index = 0;
         index < sizeof operators / sizeof operators[0]; index++) {
        size_t operator_length = strlen(operators[index].text);

        if (available < operator_length) {
            continue;
        }
        if (memcmp(text, operators[index].text, operator_length) == 0) {
            *length = operator_length;
            return operators[index].kind;
        }
    }
    return WL_TOK_EOF; /* not an operator; caller distinguishes by length */
}

bool wl_lex_next(struct wl_lexer *lexer, struct wl_token *token) {
    memset(token, 0, sizeof *token);

    for (;;) {
        while (!wl_lex_done(lexer) &&
               (wl_lex_peek_at(lexer, 0) == ' ' ||
                wl_lex_peek_at(lexer, 0) == '\t')) {
            wl_lex_skip(lexer, 1);
        }
        if (!wl_lex_done(lexer) && wl_lex_peek_at(lexer, 0) == '#') {
            while (!wl_lex_done(lexer) && wl_lex_peek_at(lexer, 0) != '\n') {
                wl_lex_skip(lexer, 1);
            }
            continue;
        }
        break;
    }

    token->span.start = lexer->pos;
    if (wl_lex_done(lexer)) {
        token->kind = WL_TOK_EOF;
        token->span.end = lexer->pos;
        return true;
    }

    {
        char first = wl_lex_peek_at(lexer, 0);

        if (first == '\n') {
            wl_lex_skip(lexer, 1);
            token->kind = WL_TOK_NEWLINE;
            token->span.end = lexer->pos;
            return true;
        }

        /* IO_NUMBER: digits immediately followed by a redirection operator. */
        if (isdigit((unsigned char)first)) {
            size_t digits = 0;

            while (!wl_lex_done(lexer) &&
                   isdigit((unsigned char)wl_lex_peek_at(lexer, digits))) {
                digits++;
            }
            if (wl_lex_peek_at(lexer, digits) == '<' ||
                wl_lex_peek_at(lexer, digits) == '>') {
                char number[32];
                size_t copy = digits < sizeof number - 1 ? digits
                                                         : sizeof number - 1;

                memcpy(number, lexer->src + lexer->pos, copy);
                number[copy] = '\0';
                token->io_number = strtol(number, NULL, 10);
                wl_lex_skip(lexer, digits);
                token->kind = WL_TOK_IO_NUMBER;
                token->span.end = lexer->pos;
                return true;
            }
        }
    }

    {
        size_t operator_length = 0;
        enum wl_tok_kind kind = wl_match_operator(
            lexer->src + lexer->pos, lexer->len - lexer->pos,
            &operator_length);

        if (operator_length > 0) {
            wl_lex_skip(lexer, operator_length);
            token->kind = kind;
            token->span.end = lexer->pos;
            return true;
        }
    }

    {
        struct wl_word *word = NULL;

        if (!wl_lex_word(lexer, &word)) {
            return false;
        }
        token->kind = WL_TOK_WORD;
        token->word = word;
        token->span = word->span;
        return true;
    }
}

bool wl_lex_read_heredoc(struct wl_lexer *lexer, const struct wl_str *delimiter,
                         bool strip_tabs, bool expand,
                         struct wl_heredoc *heredoc) {
    struct wl_buf body;
    bool ok = false;

    wl_buf_init(&body);

    while (!wl_lex_done(lexer)) {
        size_t line_start = lexer->pos;
        size_t line_end = lexer->pos;
        size_t compared = line_start;

        while (line_end < lexer->len && lexer->src[line_end] != '\n') {
            line_end++;
        }

        if (strip_tabs) {
            while (compared < line_end && lexer->src[compared] == '\t') {
                compared++;
            }
        }

        if (line_end - compared == delimiter->len &&
            memcmp(lexer->src + compared, delimiter->data, delimiter->len) ==
                0) {
            heredoc->body.data =
                wl_arena_str(lexer->arena, body.data, body.len);
            heredoc->body.len = body.len;
            heredoc->expand = expand;
            heredoc->strip_tabs = strip_tabs;
            if (heredoc->body.data == NULL) {
                wl_diag_set(lexer->diag, line_start, false,
                            "out of memory while reading here-document");
                goto done;
            }
            lexer->pos = (line_end < lexer->len) ? line_end + 1 : line_end;
            ok = true;
            goto done;
        }

        if (!wl_buf_append(&body, lexer->src + compared, line_end - compared) ||
            (line_end < lexer->len && !wl_buf_append_char(&body, '\n'))) {
            wl_diag_set(lexer->diag, line_start, false,
                        "out of memory while reading here-document");
            goto done;
        }

        lexer->pos = (line_end < lexer->len) ? line_end + 1 : line_end;
    }

    wl_diag_set(lexer->diag, lexer->pos, true,
                "here-document delimited by end of file (wanted '%.*s')",
                (int)delimiter->len, delimiter->data);

done:
    wl_buf_free(&body);
    return ok;
}
