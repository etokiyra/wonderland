#include "parse/parse.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ast/arena.h"
#include "core/buf.h"
#include "lex/lex.h"

/*
 * Recursive-descent parser for docs/GRAMMAR.md.
 *
 * Pulls one token at a time from the lexer. Here-documents are the only place
 * where the order matters mechanically: bodies live after the newline that
 * ends the command line, so they are read when that newline is *consumed*,
 * before the next token is scanned (wl_p_consume).
 *
 * Errors are fail-fast with one diagnostic. `incomplete` distinguishes "the
 * input stopped too early" from "this is wrong"; the interactive loop uses it
 * for a continuation prompt.
 */

#define WL_VECTOR(type_name, element_type)                                    \
    struct type_name {                                                        \
        element_type *items;                                                  \
        size_t count;                                                         \
        size_t capacity;                                                      \
    };                                                                        \
    static bool type_name##_push(struct type_name *vector,                    \
                                 element_type value) {                        \
        if (vector->count == vector->capacity) {                              \
            size_t capacity =                                                 \
                (vector->capacity > 0) ? vector->capacity * 2 : 8;            \
            element_type *grown;                                              \
            if (capacity > SIZE_MAX / sizeof *vector->items) {                \
                return false;                                                 \
            }                                                                 \
            grown = realloc(vector->items, capacity * sizeof *vector->items); \
            if (grown == NULL) {                                              \
                return false;                                                 \
            }                                                                 \
            vector->items = grown;                                            \
            vector->capacity = capacity;                                      \
        }                                                                     \
        vector->items[vector->count] = value;                                 \
        vector->count++;                                                      \
        return true;                                                          \
    }                                                                         \
    static void type_name##_free(struct type_name *vector) {                  \
        free(vector->items);                                                  \
        vector->items = NULL;                                                 \
        vector->count = 0;                                                    \
        vector->capacity = 0;                                                 \
    }

WL_VECTOR(wl_node_vec, struct wl_node *)
WL_VECTOR(wl_word_vec, struct wl_word *)
WL_VECTOR(wl_str_vec, struct wl_str)
WL_VECTOR(wl_redir_vec, struct wl_redir *)
WL_VECTOR(wl_andor_vec, struct wl_andor_link)
WL_VECTOR(wl_arm_vec, struct wl_if_arm)
WL_VECTOR(wl_item_vec, struct wl_case_item)

struct wl_stmt {
    struct wl_node *node;
    bool async;
};

WL_VECTOR(wl_stmt_vec, struct wl_stmt)

struct wl_pending_heredoc {
    struct wl_redir *redir;
    struct wl_str delimiter;
    bool strip_tabs;
    bool expand;
};

struct wl_parser {
    struct wl_lexer lexer;
    struct wl_token token;
    struct wl_token ahead;
    bool has_ahead;
    struct wl_arena *arena;
    struct wl_diag *diag;
    int depth;
    struct wl_pending_heredoc *pending;
    size_t pending_count;
    size_t pending_capacity;
};

static struct wl_node *wl_p_parse_list(struct wl_parser *parser,
                                       bool allow_empty);

static void *wl_p_fail(struct wl_parser *parser, bool incomplete,
                       const char *format, ...) {
    char message[256];
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(message, sizeof message, format, arguments);
    va_end(arguments);

    wl_diag_set(parser->diag, parser->token.span.start, incomplete, "%s",
                message);
    return NULL;
}

static bool wl_p_next(struct wl_parser *parser) {
    if (parser->has_ahead) {
        parser->token = parser->ahead;
        parser->has_ahead = false;
        return true;
    }
    return wl_lex_next(&parser->lexer, &parser->token);
}

static bool wl_p_collect_heredocs(struct wl_parser *parser) {
    for (size_t index = 0; index < parser->pending_count; index++) {
        struct wl_pending_heredoc *item = &parser->pending[index];

        if (!wl_lex_read_heredoc(&parser->lexer, &item->delimiter,
                                 item->strip_tabs, item->expand,
                                 item->redir->heredoc)) {
            return false;
        }
    }
    parser->pending_count = 0;
    return true;
}

static bool wl_p_consume(struct wl_parser *parser) {
    if (parser->token.kind == WL_TOK_NEWLINE && parser->pending_count > 0) {
        if (!wl_p_collect_heredocs(parser)) {
            return false;
        }
    }
    return wl_p_next(parser);
}

static bool wl_p_skip_newlines(struct wl_parser *parser) {
    while (parser->token.kind == WL_TOK_NEWLINE) {
        if (!wl_p_consume(parser)) {
            return false;
        }
    }
    return true;
}

static const struct wl_token *wl_p_peek(struct wl_parser *parser) {
    /*
     * Peeking across a newline with pending here-documents would scan the
     * body as commands. No call site does that: peeking only happens at
     * command start, after newlines have been consumed.
     */
    if (parser->token.kind == WL_TOK_NEWLINE && parser->pending_count > 0) {
        wl_diag_set(parser->diag, parser->token.span.start, false,
                    "internal error: lookahead across a here-document");
        return NULL;
    }
    if (!parser->has_ahead) {
        if (!wl_lex_next(&parser->lexer, &parser->ahead)) {
            return NULL;
        }
        parser->has_ahead = true;
    }
    return &parser->ahead;
}

static bool wl_p_is_name(const struct wl_str *name) {
    if (name->len == 0) {
        return false;
    }
    if (!((name->data[0] >= 'a' && name->data[0] <= 'z') ||
          (name->data[0] >= 'A' && name->data[0] <= 'Z') ||
          name->data[0] == '_')) {
        return false;
    }
    for (size_t index = 1; index < name->len; index++) {
        char c = name->data[index];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

static bool wl_p_word_is(const struct wl_word *word, const char *text) {    size_t length = strlen(text);
    const struct wl_word_part *part;

    if (word == NULL || word->count != 1) {
        return false;
    }
    part = &word->parts[0];
    if (part->kind != WL_WP_LITERAL || part->quoted) {
        return false;
    }
    return part->u.literal.len == length &&
           memcmp(part->u.literal.data, text, length) == 0;
}

static bool wl_p_at_keyword(struct wl_parser *parser, const char *text) {
    return parser->token.kind == WL_TOK_WORD &&
           wl_p_word_is(parser->token.word, text);
}

static bool wl_p_expect_keyword(struct wl_parser *parser, const char *text) {
    if (!wl_p_at_keyword(parser, text)) {
        (void)wl_p_fail(parser, parser->token.kind == WL_TOK_EOF,
                        "expected '%s'", text);
        return false;
    }
    return wl_p_consume(parser);
}

static bool wl_p_expect(struct wl_parser *parser, enum wl_tok_kind kind,
                        const char *what) {
    if (parser->token.kind != kind) {
        (void)wl_p_fail(parser, parser->token.kind == WL_TOK_EOF,
                        "expected %s", what);
        return false;
    }
    return wl_p_consume(parser);
}

static bool wl_p_at_list_end(struct wl_parser *parser) {
    static const char *const stops[] = {"then", "elif", "else", "fi",
                                        "do",   "done", "esac", "}"};

    if (parser->token.kind == WL_TOK_EOF ||
        parser->token.kind == WL_TOK_RPAREN ||
        parser->token.kind == WL_TOK_DSEMI) {
        return true;
    }
    if (parser->token.kind != WL_TOK_WORD) {
        return false;
    }
    for (size_t index = 0; index < sizeof stops / sizeof stops[0]; index++) {
        if (wl_p_at_keyword(parser, stops[index])) {
            return true;
        }
    }
    return false;
}

static bool wl_p_is_redir_op(enum wl_tok_kind kind) {
    return kind >= WL_TOK_LESS && kind <= WL_TOK_GREATAND;
}

static bool wl_p_at_redir(struct wl_parser *parser) {
    return parser->token.kind == WL_TOK_IO_NUMBER ||
           wl_p_is_redir_op(parser->token.kind);
}

static enum wl_redir_op wl_p_redir_op(enum wl_tok_kind kind) {
    switch (kind) {
    case WL_TOK_LESS:
        return WL_REDIR_IN;
    case WL_TOK_GREAT:
        return WL_REDIR_OUT;
    case WL_TOK_DGREAT:
        return WL_REDIR_APPEND;
    case WL_TOK_LESSGREAT:
        return WL_REDIR_READWRITE;
    case WL_TOK_CLOBBER:
        return WL_REDIR_CLOBBER;
    case WL_TOK_LESSAND:
        return WL_REDIR_DUP_IN;
    case WL_TOK_GREATAND:
        return WL_REDIR_DUP_OUT;
    case WL_TOK_DLESS:
        return WL_REDIR_HEREDOC;
    case WL_TOK_DLESSDASH:
        return WL_REDIR_HEREDOC_TABS;
    case WL_TOK_TLESS:
        return WL_REDIR_HERESTRING;
    default:
        return WL_REDIR_IN;
    }
}

static int wl_p_default_fd(enum wl_redir_op op) {
    switch (op) {
    case WL_REDIR_OUT:
    case WL_REDIR_APPEND:
    case WL_REDIR_CLOBBER:
    case WL_REDIR_DUP_OUT:
        return 1;
    case WL_REDIR_IN:
    case WL_REDIR_READWRITE:
    case WL_REDIR_DUP_IN:
    case WL_REDIR_HEREDOC:
    case WL_REDIR_HEREDOC_TABS:
    case WL_REDIR_HERESTRING:
    default:
        return 0;
    }
}

static bool wl_p_pending_add(struct wl_parser *parser, struct wl_redir *redir,
                             struct wl_str delimiter, bool strip_tabs,
                             bool expand) {
    if (parser->pending_count == parser->pending_capacity) {
        size_t capacity =
            (parser->pending_capacity > 0) ? parser->pending_capacity * 2 : 4;
        struct wl_pending_heredoc *grown = realloc(
            parser->pending, capacity * sizeof *parser->pending);

        if (grown == NULL) {
            return false;
        }
        parser->pending = grown;
        parser->pending_capacity = capacity;
    }
    parser->pending[parser->pending_count].redir = redir;
    parser->pending[parser->pending_count].delimiter = delimiter;
    parser->pending[parser->pending_count].strip_tabs = strip_tabs;
    parser->pending[parser->pending_count].expand = expand;
    parser->pending_count++;
    return true;
}

static struct wl_redir *wl_p_parse_redir(struct wl_parser *parser) {
    int fd = -1;
    size_t start = parser->token.span.start;
    enum wl_tok_kind kind;
    struct wl_redir *redir;
    struct wl_word *target;

    if (parser->token.kind == WL_TOK_IO_NUMBER) {
        fd = (int)parser->token.io_number;
        if (!wl_p_consume(parser)) {
            return NULL;
        }
    }

    kind = parser->token.kind;
    if (!wl_p_is_redir_op(kind)) {
        return wl_p_fail(parser, false, "expected a redirection operator");
    }
    if (!wl_p_consume(parser)) {
        return NULL;
    }
    if (parser->token.kind != WL_TOK_WORD || parser->token.word == NULL) {
        return wl_p_fail(parser, parser->token.kind == WL_TOK_EOF,
                         "expected a word after '%s'", wl_tok_name(kind));
    }
    target = parser->token.word;

    redir = wl_arena_alloc(parser->arena, sizeof *redir);
    if (redir == NULL) {
        return wl_p_fail(parser, false, "out of memory");
    }
    redir->op = wl_p_redir_op(kind);
    redir->fd = (fd >= 0) ? fd : wl_p_default_fd(redir->op);
    redir->target = target;
    redir->heredoc = NULL;
    redir->span.start = start;
    redir->span.end = parser->token.span.end;

    if (redir->op == WL_REDIR_HEREDOC ||
        redir->op == WL_REDIR_HEREDOC_TABS) {
        struct wl_heredoc *heredoc =
            wl_arena_alloc(parser->arena, sizeof *heredoc);
        struct wl_buf literal;
        struct wl_str delimiter;
        bool strip_tabs = redir->op == WL_REDIR_HEREDOC_TABS;
        bool ok;

        if (heredoc == NULL) {
            return wl_p_fail(parser, false, "out of memory");
        }
        wl_buf_init(&literal);
        ok = wl_word_literal(target, &literal);
        if (ok) {
            delimiter.len = literal.len;
            delimiter.data =
                wl_arena_str(parser->arena, literal.data, literal.len);
            ok = delimiter.data != NULL;
        }
        wl_buf_free(&literal);
        if (!ok) {
            return wl_p_fail(parser, false,
                             "here-document delimiter must be a literal");
        }

        redir->heredoc = heredoc;
        if (!wl_p_pending_add(parser, redir, delimiter, strip_tabs,
                              !target->quoted)) {
            return wl_p_fail(parser, false, "out of memory");
        }
    }

    if (!wl_p_consume(parser)) {
        return NULL;
    }
    return redir;
}

static bool wl_p_attach_redirs(struct wl_parser *parser, struct wl_node *node,
                               struct wl_redir **redirs, size_t count) {
    struct wl_redir **combined;

    if (count == 0) {
        return true;
    }
    combined = wl_arena_calloc(parser->arena, node->redir_count + count,
                               sizeof *combined);
    if (combined == NULL) {
        (void)wl_p_fail(parser, false, "out of memory");
        return false;
    }
    for (size_t index = 0; index < node->redir_count; index++) {
        combined[index] = node->redirs[index];
    }
    for (size_t index = 0; index < count; index++) {
        combined[node->redir_count + index] = redirs[index];
    }
    node->redirs = combined;
    node->redir_count += count;
    return true;
}

static bool wl_p_parse_trailing_redirs(struct wl_parser *parser,
                                       struct wl_node *node) {
    struct wl_redir_vec redirs = {0};
    bool ok = true;

    while (ok && wl_p_at_redir(parser)) {
        struct wl_redir *redir = wl_p_parse_redir(parser);

        if (redir == NULL) {
            ok = false;
        } else if (!wl_redir_vec_push(&redirs, redir)) {
            (void)wl_p_fail(parser, false, "out of memory");
            ok = false;
        }
    }
    if (ok) {
        ok = wl_p_attach_redirs(parser, node, redirs.items, redirs.count);
    }
    wl_redir_vec_free(&redirs);
    return ok;
}

static struct wl_node *wl_p_parse_command(struct wl_parser *parser);

static struct wl_node *wl_p_parse_simple(struct wl_parser *parser) {
    struct wl_node *node;
    struct wl_str_vec assign_names = {0};
    struct wl_word_vec assign_values = {0};
    struct wl_word_vec words = {0};
    struct wl_redir_vec redirs = {0};
    size_t start = parser->token.span.start;
    bool any = false;

    for (;;) {
        if (wl_p_at_redir(parser)) {
            struct wl_redir *redir = wl_p_parse_redir(parser);

            if (redir == NULL) {
                goto fail;
            }
            if (!wl_redir_vec_push(&redirs, redir)) {
                (void)wl_p_fail(parser, false, "out of memory");
                goto fail;
            }
            any = true;
            continue;
        }

        if (parser->token.kind == WL_TOK_WORD && parser->token.word != NULL) {
            if (words.count == 0) {
                struct wl_str name;
                struct wl_word *value = NULL;

                if (wl_word_split_assignment(parser->arena, parser->token.word,
                                             &name, &value)) {
                    if (!wl_str_vec_push(&assign_names, name) ||
                        !wl_word_vec_push(&assign_values, value)) {
                        (void)wl_p_fail(parser, false, "out of memory");
                        goto fail;
                    }
                    any = true;
                    if (!wl_p_consume(parser)) {
                        goto fail;
                    }
                    continue;
                }
            }
            if (!wl_word_vec_push(&words, parser->token.word)) {
                (void)wl_p_fail(parser, false, "out of memory");
                goto fail;
            }
            any = true;
            if (!wl_p_consume(parser)) {
                goto fail;
            }
            continue;
        }
        break;
    }

    if (!any) {
        (void)wl_p_fail(parser, parser->token.kind == WL_TOK_EOF,
                        "expected a command");
        goto fail;
    }

    node = wl_ast_node(parser->arena, WL_NODE_SIMPLE,
                       (struct wl_span){start, parser->token.span.start});
    if (node == NULL) {
        (void)wl_p_fail(parser, false, "out of memory");
        goto fail;
    }

    node->u.simple.assign_count = assign_names.count;
    if (assign_names.count > 0) {
        node->u.simple.assign_names = wl_arena_calloc(
            parser->arena, assign_names.count, sizeof *node->u.simple.assign_names);
        node->u.simple.assign_values = wl_arena_calloc(
            parser->arena, assign_values.count,
            sizeof *node->u.simple.assign_values);
        if (node->u.simple.assign_names == NULL ||
            node->u.simple.assign_values == NULL) {
            (void)wl_p_fail(parser, false, "out of memory");
            goto fail;
        }
        memcpy(node->u.simple.assign_names, assign_names.items,
               assign_names.count * sizeof *assign_names.items);
        memcpy(node->u.simple.assign_values, assign_values.items,
               assign_values.count * sizeof *assign_values.items);
    }

    node->u.simple.word_count = words.count;
    if (words.count > 0) {
        node->u.simple.words = wl_arena_calloc(parser->arena, words.count,
                                               sizeof *node->u.simple.words);
        if (node->u.simple.words == NULL) {
            (void)wl_p_fail(parser, false, "out of memory");
            goto fail;
        }
        memcpy(node->u.simple.words, words.items,
               words.count * sizeof *words.items);
    }

    if (!wl_p_attach_redirs(parser, node, redirs.items, redirs.count)) {
        goto fail;
    }

    wl_str_vec_free(&assign_names);
    wl_word_vec_free(&assign_values);
    wl_word_vec_free(&words);
    wl_redir_vec_free(&redirs);
    return node;

fail:
    wl_str_vec_free(&assign_names);
    wl_word_vec_free(&assign_values);
    wl_word_vec_free(&words);
    wl_redir_vec_free(&redirs);
    return NULL;
}

static struct wl_node *wl_p_parse_group(struct wl_parser *parser) {
    struct wl_node *node;
    struct wl_node *body;
    size_t start = parser->token.span.start;

    if (!wl_p_consume(parser)) { /* '{' */
        return NULL;
    }
    body = wl_p_parse_list(parser, false);
    if (body == NULL) {
        return NULL;
    }
    if (!wl_p_expect_keyword(parser, "}")) {
        return NULL;
    }
    node = wl_ast_node(parser->arena, WL_NODE_GROUP,
                       (struct wl_span){start, parser->token.span.start});
    if (node == NULL) {
        return wl_p_fail(parser, false, "out of memory");
    }
    node->u.group.body = body;
    return node;
}

static struct wl_node *wl_p_parse_subshell(struct wl_parser *parser) {
    struct wl_node *node;
    struct wl_node *body;
    size_t start = parser->token.span.start;

    if (!wl_p_consume(parser)) { /* '(' */
        return NULL;
    }
    body = wl_p_parse_list(parser, false);
    if (body == NULL) {
        return NULL;
    }
    if (!wl_p_expect(parser, WL_TOK_RPAREN, "')'")) {
        return NULL;
    }
    node = wl_ast_node(parser->arena, WL_NODE_SUBSHELL,
                       (struct wl_span){start, parser->token.span.start});
    if (node == NULL) {
        return wl_p_fail(parser, false, "out of memory");
    }
    node->u.group.body = body;
    return node;
}

static struct wl_node *wl_p_parse_if(struct wl_parser *parser) {
    struct wl_node *node;
    struct wl_arm_vec arms = {0};
    struct wl_node *else_body = NULL;
    size_t start = parser->token.span.start;

    if (!wl_p_consume(parser)) { /* if */
        goto fail;
    }
    for (;;) {
        struct wl_if_arm arm;

        arm.condition = wl_p_parse_list(parser, false);
        if (arm.condition == NULL) {
            goto fail;
        }
        if (!wl_p_expect_keyword(parser, "then")) {
            goto fail;
        }
        arm.body = wl_p_parse_list(parser, false);
        if (arm.body == NULL) {
            goto fail;
        }
        if (!wl_arm_vec_push(&arms, arm)) {
            (void)wl_p_fail(parser, false, "out of memory");
            goto fail;
        }
        if (wl_p_at_keyword(parser, "elif")) {
            if (!wl_p_consume(parser)) {
                goto fail;
            }
            continue;
        }
        break;
    }
    if (wl_p_at_keyword(parser, "else")) {
        if (!wl_p_consume(parser)) {
            goto fail;
        }
        else_body = wl_p_parse_list(parser, false);
        if (else_body == NULL) {
            goto fail;
        }
    }
    if (!wl_p_expect_keyword(parser, "fi")) {
        goto fail;
    }

    node = wl_ast_node(parser->arena, WL_NODE_IF,
                       (struct wl_span){start, parser->token.span.start});
    if (node == NULL) {
        (void)wl_p_fail(parser, false, "out of memory");
        goto fail;
    }
    node->u.ifclause.arm_count = arms.count;
    node->u.ifclause.arms = wl_arena_calloc(parser->arena, arms.count,
                                            sizeof *node->u.ifclause.arms);
    if (node->u.ifclause.arms == NULL) {
        (void)wl_p_fail(parser, false, "out of memory");
        goto fail;
    }
    memcpy(node->u.ifclause.arms, arms.items,
           arms.count * sizeof *arms.items);
    node->u.ifclause.else_body = else_body;
    wl_arm_vec_free(&arms);
    return node;

fail:
    wl_arm_vec_free(&arms);
    return NULL;
}

static struct wl_node *wl_p_parse_while(struct wl_parser *parser, bool until) {
    struct wl_node *node;
    struct wl_node *condition;
    struct wl_node *body;
    size_t start = parser->token.span.start;

    if (!wl_p_consume(parser)) { /* while / until */
        return NULL;
    }
    condition = wl_p_parse_list(parser, false);
    if (condition == NULL) {
        return NULL;
    }
    if (!wl_p_expect_keyword(parser, "do")) {
        return NULL;
    }
    body = wl_p_parse_list(parser, false);
    if (body == NULL) {
        return NULL;
    }
    if (!wl_p_expect_keyword(parser, "done")) {
        return NULL;
    }
    node = wl_ast_node(parser->arena, WL_NODE_WHILE,
                       (struct wl_span){start, parser->token.span.start});
    if (node == NULL) {
        return wl_p_fail(parser, false, "out of memory");
    }
    node->u.whileloop.until = until;
    node->u.whileloop.condition = condition;
    node->u.whileloop.body = body;
    return node;
}

static struct wl_node *wl_p_parse_for(struct wl_parser *parser) {
    struct wl_node *node;
    struct wl_node *body;
    struct wl_word_vec words = {0};
    struct wl_str name;
    bool words_given = false;
    size_t start = parser->token.span.start;

    if (!wl_p_consume(parser)) { /* for */
        goto fail;
    }
    if (parser->token.kind != WL_TOK_WORD || parser->token.word == NULL) {
        (void)wl_p_fail(parser, false, "expected a name after 'for'");
        goto fail;
    }
    {
        struct wl_buf literal;
        bool ok;

        wl_buf_init(&literal);
        ok = wl_word_literal(parser->token.word, &literal);
        if (ok) {
            name.len = literal.len;
            name.data = wl_arena_str(parser->arena, literal.data, literal.len);
            ok = name.data != NULL;
        }
        wl_buf_free(&literal);
        if (!ok) {
            (void)wl_p_fail(parser, false, "expected a name after 'for'");
            goto fail;
        }
    }
    if (!wl_p_is_name(&name)) {
        (void)wl_p_fail(parser, false, "expected a name after 'for'");
        goto fail;
    }
    if (!wl_p_consume(parser)) {
        goto fail;
    }

    if (wl_p_at_keyword(parser, "in")) {
        words_given = true;
        if (!wl_p_consume(parser)) {
            goto fail;
        }
        while (parser->token.kind == WL_TOK_WORD && parser->token.word != NULL) {
            if (!wl_word_vec_push(&words, parser->token.word)) {
                (void)wl_p_fail(parser, false, "out of memory");
                goto fail;
            }
            if (!wl_p_consume(parser)) {
                goto fail;
            }
        }
    }

    if (parser->token.kind == WL_TOK_SEMI) {
        if (!wl_p_consume(parser)) {
            goto fail;
        }
    }
    if (!wl_p_skip_newlines(parser)) {
        goto fail;
    }
    if (!wl_p_expect_keyword(parser, "do")) {
        goto fail;
    }
    body = wl_p_parse_list(parser, false);
    if (body == NULL) {
        goto fail;
    }
    if (!wl_p_expect_keyword(parser, "done")) {
        goto fail;
    }

    node = wl_ast_node(parser->arena, WL_NODE_FOR,
                       (struct wl_span){start, parser->token.span.start});
    if (node == NULL) {
        (void)wl_p_fail(parser, false, "out of memory");
        goto fail;
    }
    node->u.forloop.name = name;
    node->u.forloop.words_given = words_given;
    node->u.forloop.word_count = words.count;
    if (words.count > 0) {
        node->u.forloop.words = wl_arena_calloc(parser->arena, words.count,
                                                sizeof *node->u.forloop.words);
        if (node->u.forloop.words == NULL) {
            (void)wl_p_fail(parser, false, "out of memory");
            goto fail;
        }
        memcpy(node->u.forloop.words, words.items,
               words.count * sizeof *words.items);
    }
    node->u.forloop.body = body;
    wl_word_vec_free(&words);
    return node;

fail:
    wl_word_vec_free(&words);
    return NULL;
}

static struct wl_node *wl_p_parse_case(struct wl_parser *parser) {
    struct wl_node *node;
    struct wl_item_vec items = {0};
    struct wl_word *word;
    size_t start = parser->token.span.start;

    if (!wl_p_consume(parser)) { /* case */
        goto fail;
    }
    if (parser->token.kind != WL_TOK_WORD || parser->token.word == NULL) {
        (void)wl_p_fail(parser, false, "expected a word after 'case'");
        goto fail;
    }
    word = parser->token.word;
    if (!wl_p_consume(parser)) {
        goto fail;
    }
    if (!wl_p_skip_newlines(parser)) {
        goto fail;
    }
    if (!wl_p_expect_keyword(parser, "in")) {
        goto fail;
    }

    for (;;) {
        struct wl_case_item item;
        struct wl_word_vec patterns = {0};

        if (!wl_p_skip_newlines(parser)) {
            wl_word_vec_free(&patterns);
            goto fail;
        }
        if (wl_p_at_keyword(parser, "esac")) {
            wl_word_vec_free(&patterns);
            break;
        }
        if (parser->token.kind == WL_TOK_EOF) {
            wl_word_vec_free(&patterns);
            (void)wl_p_fail(parser, true, "expected 'esac'");
            goto fail;
        }
        if (parser->token.kind == WL_TOK_LPAREN) {
            if (!wl_p_consume(parser)) {
                wl_word_vec_free(&patterns);
                goto fail;
            }
        }
        for (;;) {
            if (parser->token.kind != WL_TOK_WORD ||
                parser->token.word == NULL) {
                wl_word_vec_free(&patterns);
                (void)wl_p_fail(parser, false, "expected a case pattern");
                goto fail;
            }
            if (!wl_word_vec_push(&patterns, parser->token.word)) {
                wl_word_vec_free(&patterns);
                (void)wl_p_fail(parser, false, "out of memory");
                goto fail;
            }
            if (!wl_p_consume(parser)) {
                wl_word_vec_free(&patterns);
                goto fail;
            }
            if (parser->token.kind != WL_TOK_PIPE) {
                break;
            }
            if (!wl_p_consume(parser)) {
                wl_word_vec_free(&patterns);
                goto fail;
            }
        }
        if (!wl_p_expect(parser, WL_TOK_RPAREN, "')'")) {
            wl_word_vec_free(&patterns);
            goto fail;
        }

        item.patterns = NULL;
        item.pattern_count = patterns.count;
        item.body = NULL;
        item.span.start = 0;
        item.span.end = 0;
        if (patterns.count > 0) {
            item.patterns = wl_arena_calloc(
                parser->arena, patterns.count, sizeof *item.patterns);
            if (item.patterns == NULL) {
                wl_word_vec_free(&patterns);
                (void)wl_p_fail(parser, false, "out of memory");
                goto fail;
            }
            memcpy(item.patterns, patterns.items,
                   patterns.count * sizeof *patterns.items);
        }
        wl_word_vec_free(&patterns);

        item.body = wl_p_parse_list(parser, false);
        if (item.body == NULL) {
            goto fail;
        }
        if (!wl_item_vec_push(&items, item)) {
            (void)wl_p_fail(parser, false, "out of memory");
            goto fail;
        }
        if (parser->token.kind == WL_TOK_DSEMI) {
            if (!wl_p_consume(parser)) {
                goto fail;
            }
            continue;
        }
        if (wl_p_at_keyword(parser, "esac")) {
            break;
        }
        if (parser->token.kind == WL_TOK_EOF) {
            (void)wl_p_fail(parser, true, "expected 'esac'");
            goto fail;
        }
        (void)wl_p_fail(parser, false, "expected ';;' or 'esac'");
        goto fail;
    }

    if (!wl_p_expect_keyword(parser, "esac")) {
        goto fail;
    }

    node = wl_ast_node(parser->arena, WL_NODE_CASE,
                       (struct wl_span){start, parser->token.span.start});
    if (node == NULL) {
        (void)wl_p_fail(parser, false, "out of memory");
        goto fail;
    }
    node->u.caseclause.word = word;
    node->u.caseclause.item_count = items.count;
    if (items.count > 0) {
        node->u.caseclause.items = wl_arena_calloc(
            parser->arena, items.count, sizeof *node->u.caseclause.items);
        if (node->u.caseclause.items == NULL) {
            (void)wl_p_fail(parser, false, "out of memory");
            goto fail;
        }
        memcpy(node->u.caseclause.items, items.items,
               items.count * sizeof *items.items);
    }
    wl_item_vec_free(&items);
    return node;

fail:
    wl_item_vec_free(&items);
    return NULL;
}

static struct wl_node *wl_p_parse_function(struct wl_parser *parser) {
    struct wl_node *node;
    struct wl_node *body;
    struct wl_str name;
    size_t start = parser->token.span.start;

    {
        struct wl_buf literal;
        bool ok;

        wl_buf_init(&literal);
        ok = wl_word_literal(parser->token.word, &literal);
        if (ok) {
            name.len = literal.len;
            name.data = wl_arena_str(parser->arena, literal.data, literal.len);
            ok = name.data != NULL;
        }
        wl_buf_free(&literal);
        if (!ok) {
            return wl_p_fail(parser, false, "invalid function name");
        }
        if (!wl_p_is_name(&name)) {
            return wl_p_fail(parser, false, "invalid function name");
        }
    }

    if (!wl_p_consume(parser)) { /* name */
        return NULL;
    }
    if (!wl_p_expect(parser, WL_TOK_LPAREN, "'('")) {
        return NULL;
    }
    if (!wl_p_expect(parser, WL_TOK_RPAREN, "')'")) {
        return NULL;
    }
    if (!wl_p_skip_newlines(parser)) {
        return NULL;
    }

    body = wl_p_parse_command(parser);
    if (body == NULL) {
        return NULL;
    }
    if (body->kind == WL_NODE_SIMPLE) {
        return wl_p_fail(parser, false,
                         "function body must be a compound command");
    }

    node = wl_ast_node(parser->arena, WL_NODE_FUNC,
                       (struct wl_span){start, parser->token.span.start});
    if (node == NULL) {
        return wl_p_fail(parser, false, "out of memory");
    }
    node->u.func.name = name;
    node->u.func.body = body;
    return node;
}

static struct wl_node *wl_p_parse_command_inner(struct wl_parser *parser) {
    struct wl_node *node;

    if (wl_p_at_keyword(parser, "if")) {
        node = wl_p_parse_if(parser);
    } else if (wl_p_at_keyword(parser, "while")) {
        node = wl_p_parse_while(parser, false);
    } else if (wl_p_at_keyword(parser, "until")) {
        node = wl_p_parse_while(parser, true);
    } else if (wl_p_at_keyword(parser, "for")) {
        node = wl_p_parse_for(parser);
    } else if (wl_p_at_keyword(parser, "case")) {
        node = wl_p_parse_case(parser);
    } else if (wl_p_at_keyword(parser, "{")) {
        node = wl_p_parse_group(parser);
    } else if (parser->token.kind == WL_TOK_LPAREN) {
        node = wl_p_parse_subshell(parser);
    } else if (parser->token.kind == WL_TOK_WORD &&
               parser->token.word != NULL) {
        const struct wl_token *ahead = wl_p_peek(parser);

        if (ahead == NULL) {
            /* Lexical error while looking for `NAME (`; the diagnostic is set. */
            return NULL;
        }
        if (ahead->kind == WL_TOK_LPAREN) {
            node = wl_p_parse_function(parser);
        } else {
            node = wl_p_parse_simple(parser);
        }
    } else if (wl_p_at_redir(parser)) {
        node = wl_p_parse_simple(parser);
    } else {
        return wl_p_fail(parser, parser->token.kind == WL_TOK_EOF,
                         "expected a command");
    }

    if (node == NULL) {
        return NULL;
    }
    if (!wl_p_parse_trailing_redirs(parser, node)) {
        return NULL;
    }
    return node;
}

static struct wl_node *wl_p_parse_command(struct wl_parser *parser) {
    struct wl_node *node;

    if (parser->depth >= WL_PARSE_MAX_DEPTH) {
        return wl_p_fail(parser, false, "nesting is too deep (max %d)",
                         WL_PARSE_MAX_DEPTH);
    }
    parser->depth++;
    node = wl_p_parse_command_inner(parser);
    parser->depth--;
    return node;
}

static struct wl_node *wl_p_parse_pipeline(struct wl_parser *parser) {
    struct wl_node *node;
    struct wl_node_vec commands = {0};
    bool bang = false;
    size_t start = parser->token.span.start;

    if (wl_p_at_keyword(parser, "!")) {
        bang = true;
        if (!wl_p_consume(parser)) {
            goto fail;
        }
    }

    for (;;) {
        struct wl_node *command = wl_p_parse_command(parser);

        if (command == NULL) {
            goto fail;
        }
        if (!wl_node_vec_push(&commands, command)) {
            (void)wl_p_fail(parser, false, "out of memory");
            goto fail;
        }
        if (parser->token.kind != WL_TOK_PIPE) {
            break;
        }
        if (!wl_p_consume(parser)) {
            goto fail;
        }
        if (!wl_p_skip_newlines(parser)) {
            goto fail;
        }
    }

    node = wl_ast_node(parser->arena, WL_NODE_PIPELINE,
                       (struct wl_span){start, parser->token.span.start});
    if (node == NULL) {
        (void)wl_p_fail(parser, false, "out of memory");
        goto fail;
    }
    node->u.pipeline.bang = bang;
    node->u.pipeline.count = commands.count;
    node->u.pipeline.commands = wl_arena_calloc(
        parser->arena, commands.count, sizeof *node->u.pipeline.commands);
    if (node->u.pipeline.commands == NULL) {
        (void)wl_p_fail(parser, false, "out of memory");
        goto fail;
    }
    memcpy(node->u.pipeline.commands, commands.items,
           commands.count * sizeof *commands.items);
    wl_node_vec_free(&commands);
    return node;

fail:
    wl_node_vec_free(&commands);
    return NULL;
}

static struct wl_node *wl_p_parse_andor(struct wl_parser *parser) {
    struct wl_node *node;
    struct wl_node *first;
    struct wl_andor_vec links = {0};
    size_t start = parser->token.span.start;

    first = wl_p_parse_pipeline(parser);
    if (first == NULL) {
        return NULL;
    }

    while (parser->token.kind == WL_TOK_AND_IF ||
           parser->token.kind == WL_TOK_OR_IF) {
        enum wl_andor_op op = (parser->token.kind == WL_TOK_AND_IF)
                                  ? WL_ANDOR_AND
                                  : WL_ANDOR_OR;
        struct wl_andor_link link;
        struct wl_node *next;

        if (!wl_p_consume(parser)) {
            goto fail;
        }
        if (!wl_p_skip_newlines(parser)) {
            goto fail;
        }
        next = wl_p_parse_pipeline(parser);
        if (next == NULL) {
            goto fail;
        }
        link.op = op;
        link.node = next;
        if (!wl_andor_vec_push(&links, link)) {
            (void)wl_p_fail(parser, false, "out of memory");
            goto fail;
        }
    }

    node = wl_ast_node(parser->arena, WL_NODE_ANDOR,
                       (struct wl_span){start, parser->token.span.start});
    if (node == NULL) {
        (void)wl_p_fail(parser, false, "out of memory");
        goto fail;
    }
    node->u.andor.first = first;
    node->u.andor.count = links.count;
    if (links.count > 0) {
        node->u.andor.links = wl_arena_calloc(parser->arena, links.count,
                                              sizeof *node->u.andor.links);
        if (node->u.andor.links == NULL) {
            (void)wl_p_fail(parser, false, "out of memory");
            goto fail;
        }
        memcpy(node->u.andor.links, links.items,
               links.count * sizeof *links.items);
    }
    wl_andor_vec_free(&links);
    return node;

fail:
    wl_andor_vec_free(&links);
    return NULL;
}

static struct wl_node *wl_p_parse_list(struct wl_parser *parser,
                                       bool allow_empty) {
    struct wl_node *node;
    struct wl_stmt_vec statements = {0};
    size_t start = parser->token.span.start;

    for (;;) {
        struct wl_node *statement;
        struct wl_stmt entry;

        if (!wl_p_skip_newlines(parser)) {
            goto fail;
        }
        if (wl_p_at_list_end(parser)) {
            break;
        }

        statement = wl_p_parse_andor(parser);
        if (statement == NULL) {
            goto fail;
        }

        entry.node = statement;
        entry.async = false;

        if (parser->token.kind == WL_TOK_SEMI) {
            if (!wl_p_consume(parser)) {
                goto fail;
            }
        } else if (parser->token.kind == WL_TOK_AMP) {
            entry.async = true;
            if (!wl_p_consume(parser)) {
                goto fail;
            }
        } else if (parser->token.kind != WL_TOK_NEWLINE &&
                   !wl_p_at_list_end(parser)) {
            (void)wl_p_fail(parser, false, "unexpected '%s'",
                            wl_tok_name(parser->token.kind));
            goto fail;
        }

        if (!wl_stmt_vec_push(&statements, entry)) {
            (void)wl_p_fail(parser, false, "out of memory");
            goto fail;
        }
    }

    if (statements.count == 0 && !allow_empty) {
        (void)wl_p_fail(parser, parser->token.kind == WL_TOK_EOF,
                        "expected a command");
        goto fail;
    }

    node = wl_ast_node(parser->arena, WL_NODE_LIST,
                       (struct wl_span){start, parser->token.span.start});
    if (node == NULL) {
        (void)wl_p_fail(parser, false, "out of memory");
        goto fail;
    }
    node->u.list.count = statements.count;
    if (statements.count > 0) {
        node->u.list.statements = wl_arena_calloc(
            parser->arena, statements.count, sizeof *node->u.list.statements);
        node->u.list.async = wl_arena_calloc(parser->arena, statements.count,
                                             sizeof *node->u.list.async);
        if (node->u.list.statements == NULL || node->u.list.async == NULL) {
            (void)wl_p_fail(parser, false, "out of memory");
            goto fail;
        }
        for (size_t index = 0; index < statements.count; index++) {
            node->u.list.statements[index] = statements.items[index].node;
            node->u.list.async[index] = statements.items[index].async;
        }
    }

    wl_stmt_vec_free(&statements);
    return node;

fail:
    wl_stmt_vec_free(&statements);
    return NULL;
}

static bool wl_p_resolve_word(struct wl_parser *parser, struct wl_word *word,
                              int depth);
static struct wl_node *wl_parse_internal(const char *source, size_t length,
                                         struct wl_arena *arena,
                                         struct wl_diag *diag, int depth);

static bool wl_p_resolve_redirs(struct wl_parser *parser,
                                struct wl_node *node, int depth) {
    for (size_t index = 0; index < node->redir_count; index++) {
        if (!wl_p_resolve_word(parser, node->redirs[index]->target, depth)) {
            return false;
        }
    }
    return true;
}

static bool wl_p_resolve_word(struct wl_parser *parser, struct wl_word *word,
                              int depth) {
    if (word == NULL) {
        return true;
    }
    for (size_t index = 0; index < word->count; index++) {
        struct wl_word_part *part = &word->parts[index];

        if (part->kind == WL_WP_PARAM) {
            if (!wl_p_resolve_word(parser, part->u.param.word, depth)) {
                return false;
            }
        } else if (part->kind == WL_WP_CMDSUB &&
                   part->u.cmdsub.program == NULL) {
            struct wl_node *program;

            if (depth >= WL_PARSE_MAX_DEPTH) {
                wl_diag_set(parser->diag, parser->token.span.start, false,
                            "command substitution nesting is too deep (max %d)",
                            WL_PARSE_MAX_DEPTH);
                return false;
            }
            program = wl_parse_internal(part->u.cmdsub.source.data,
                                        part->u.cmdsub.source.len,
                                        parser->arena, parser->diag,
                                        depth + 1);
            if (program == NULL) {
                return false;
            }
            part->u.cmdsub.program = program;
        }
    }
    return true;
}

static bool wl_p_resolve_node(struct wl_parser *parser, struct wl_node *node,
                              int depth) {
    bool ok = true;

    switch (node->kind) {
    case WL_NODE_LIST:
        for (size_t index = 0; ok && index < node->u.list.count; index++) {
            ok = wl_p_resolve_node(parser, node->u.list.statements[index],
                                   depth);
        }
        break;
    case WL_NODE_ANDOR:
        ok = wl_p_resolve_node(parser, node->u.andor.first, depth);
        for (size_t index = 0; ok && index < node->u.andor.count; index++) {
            ok = wl_p_resolve_node(parser, node->u.andor.links[index].node,
                                   depth);
        }
        break;
    case WL_NODE_PIPELINE:
        for (size_t index = 0; ok && index < node->u.pipeline.count; index++) {
            ok = wl_p_resolve_node(parser, node->u.pipeline.commands[index],
                                   depth);
        }
        break;
    case WL_NODE_SIMPLE:
        for (size_t index = 0;
             ok && index < node->u.simple.assign_count; index++) {
            ok = wl_p_resolve_word(parser, node->u.simple.assign_values[index],
                                   depth);
        }
        for (size_t index = 0; ok && index < node->u.simple.word_count;
             index++) {
            ok = wl_p_resolve_word(parser, node->u.simple.words[index], depth);
        }
        break;
    case WL_NODE_SUBSHELL:
    case WL_NODE_GROUP:
        ok = wl_p_resolve_node(parser, node->u.group.body, depth);
        break;
    case WL_NODE_FOR:
        for (size_t index = 0; ok && index < node->u.forloop.word_count;
             index++) {
            ok = wl_p_resolve_word(parser, node->u.forloop.words[index],
                                   depth);
        }
        if (ok) {
            ok = wl_p_resolve_node(parser, node->u.forloop.body, depth);
        }
        break;
    case WL_NODE_WHILE:
        ok = wl_p_resolve_node(parser, node->u.whileloop.condition, depth) &&
             wl_p_resolve_node(parser, node->u.whileloop.body, depth);
        break;
    case WL_NODE_IF:
        for (size_t index = 0; ok && index < node->u.ifclause.arm_count;
             index++) {
            ok = wl_p_resolve_node(
                     parser, node->u.ifclause.arms[index].condition, depth) &&
                 wl_p_resolve_node(parser, node->u.ifclause.arms[index].body,
                                   depth);
        }
        if (ok && node->u.ifclause.else_body != NULL) {
            ok = wl_p_resolve_node(parser, node->u.ifclause.else_body, depth);
        }
        break;
    case WL_NODE_CASE:
        ok = wl_p_resolve_word(parser, node->u.caseclause.word, depth);
        for (size_t index = 0; ok && index < node->u.caseclause.item_count;
             index++) {
            struct wl_case_item *item = &node->u.caseclause.items[index];

            for (size_t pattern = 0; ok && pattern < item->pattern_count;
                 pattern++) {
                ok = wl_p_resolve_word(parser, item->patterns[pattern], depth);
            }
            if (ok) {
                ok = wl_p_resolve_node(parser, item->body, depth);
            }
        }
        break;
    case WL_NODE_FUNC:
        ok = wl_p_resolve_node(parser, node->u.func.body, depth);
        break;
    }

    if (ok) {
        ok = wl_p_resolve_redirs(parser, node, depth);
    }
    return ok;
}

static struct wl_node *wl_parse_internal(const char *source, size_t length,
                                         struct wl_arena *arena,
                                         struct wl_diag *diag, int depth) {
    struct wl_parser parser;
    struct wl_node *program = NULL;

    memset(&parser, 0, sizeof parser);
    parser.arena = arena;
    parser.diag = diag;
    wl_lex_init(&parser.lexer, source, length, arena, diag);

    if (!wl_p_next(&parser)) {
        return NULL;
    }

    program = wl_p_parse_list(&parser, true);
    if (program == NULL) {
        free(parser.pending);
        return NULL;
    }
    if (parser.pending_count > 0) {
        wl_diag_set(diag, parser.lexer.pos, true,
                    "here-document at end of input");
        free(parser.pending);
        return NULL;
    }
    if (parser.token.kind != WL_TOK_EOF) {
        (void)wl_p_fail(&parser, false, "unexpected '%s'",
                        wl_tok_name(parser.token.kind));
        free(parser.pending);
        return NULL;
    }
    if (diag->set) {
        /* A diagnostic must never be silently swallowed by a successful parse. */
        free(parser.pending);
        return NULL;
    }
    if (!wl_p_resolve_node(&parser, program, depth)) {
        free(parser.pending);
        return NULL;
    }

    free(parser.pending);
    return program;
}

struct wl_node *wl_parse_source(const char *source, size_t length,
                                const char *name, struct wl_arena *arena,
                                struct wl_diag *diag) {
    (void)name; /* the caller owns naming in diagnostics */
    return wl_parse_internal(source, length, arena, diag, 0);
}
