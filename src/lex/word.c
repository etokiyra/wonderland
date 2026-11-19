#include "lex/lex.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ast/arena.h"
#include "core/buf.h"

/*
 * Word scanner.
 *
 * Produces the part list described in docs/GRAMMAR.md §2.3. Quoting is data,
 * not a later re-parse: a literal part's text is quote-removed, and its
 * `quoted` flag records where it came from. A new part starts whenever the
 * quoting state changes, so `x*a\ *b` splits into unquoted, quoted and
 * unquoted runs and expansion can tell which bytes may split or glob.
 *
 * Command substitutions keep their inner source here; the parser turns that
 * source into a sub-program (the lexer has no parser dependency).
 */

struct wl_part_vector {
    struct wl_word_part *items;
    size_t count;
    size_t capacity;
};

struct wl_scanner {
    struct wl_lexer *lexer;
    struct wl_part_vector parts;
    struct wl_buf literal;
    int literal_state; /* 0 none, 'u' unquoted, 'q' quoted */
    char stop;         /* 0, or '}' inside ${...} */
    bool heredoc;      /* body mode: whitespace and quotes are literal */
};

static bool wl_is_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool wl_is_name_char(char c) {
    return wl_is_name_start(c) || (c >= '0' && c <= '9');
}

static bool wl_is_special(char c) {
    return c == '@' || c == '*' || c == '?' || c == '$' || c == '#' ||
           c == '!' || c == '-';
}

static void wl_parts_free(struct wl_part_vector *parts) {
    free(parts->items);
    parts->items = NULL;
    parts->count = 0;
    parts->capacity = 0;
}

static bool wl_parts_push(struct wl_part_vector *parts,
                          const struct wl_word_part *part) {
    if (parts->count == parts->capacity) {
        size_t capacity = (parts->capacity > 0) ? parts->capacity * 2 : 8;
        struct wl_word_part *grown;

        if (capacity > SIZE_MAX / sizeof *parts->items) {
            return false;
        }
        grown = realloc(parts->items, capacity * sizeof *parts->items);
        if (grown == NULL) {
            return false;
        }
        parts->items = grown;
        parts->capacity = capacity;
    }
    parts->items[parts->count] = *part;
    parts->count++;
    return true;
}

static void wl_scan_init(struct wl_scanner *scan, struct wl_lexer *lexer,
                         char stop, bool heredoc) {
    scan->lexer = lexer;
    scan->parts.items = NULL;
    scan->parts.count = 0;
    scan->parts.capacity = 0;
    wl_buf_init(&scan->literal);
    scan->literal_state = 0;
    scan->stop = stop;
    scan->heredoc = heredoc;
}

static void wl_scan_free(struct wl_scanner *scan) {
    wl_parts_free(&scan->parts);
    wl_buf_free(&scan->literal);
}

static bool wl_scan_fail(struct wl_scanner *scan, bool incomplete,
                         const char *message) {
    wl_diag_set(scan->lexer->diag, scan->lexer->pos, incomplete, "%s", message);
    return false;
}

static bool wl_scan_flush(struct wl_scanner *scan, bool force) {
    struct wl_word_part part;

    if (scan->literal_state == 0 && !force) {
        return true;
    }
    if (scan->literal.len == 0 && !force) {
        scan->literal_state = 0;
        return true;
    }

    memset(&part, 0, sizeof part);
    part.kind = WL_WP_LITERAL;
    part.quoted = (scan->literal_state == 'q');
    part.u.literal.len = scan->literal.len;
    part.u.literal.data =
        wl_arena_str(scan->lexer->arena, scan->literal.data,
                     scan->literal.len);
    if (part.u.literal.data == NULL) {
        return wl_scan_fail(scan, false, "out of memory");
    }
    if (!wl_parts_push(&scan->parts, &part)) {
        return wl_scan_fail(scan, false, "out of memory");
    }

    wl_buf_clear(&scan->literal);
    scan->literal_state = 0;
    return true;
}

static bool wl_scan_append(struct wl_scanner *scan, const char *data,
                           size_t len, int state) {
    if (scan->literal_state != 0 && scan->literal_state != state) {
        if (!wl_scan_flush(scan, false)) {
            return false;
        }
    }
    scan->literal_state = state;
    if (!wl_buf_append(&scan->literal, data, len)) {
        return wl_scan_fail(scan, false, "out of memory");
    }
    return true;
}

static bool wl_scan_append_byte(struct wl_scanner *scan, char c, int state) {
    return wl_scan_append(scan, &c, 1, state);
}

static bool wl_scan_push(struct wl_scanner *scan,
                         const struct wl_word_part *part) {
    if (!wl_scan_flush(scan, false)) {
        return false;
    }
    if (!wl_parts_push(&scan->parts, part)) {
        return wl_scan_fail(scan, false, "out of memory");
    }
    return true;
}

static bool wl_scan_delimiter(char c, char stop, bool heredoc) {
    if (heredoc) {
        return false;
    }
    if (stop != 0) {
        /* Inside ${...}: only an unquoted `}` ends the operand word. */
        return c == stop;
    }
    if (c == ' ' || c == '\t' || c == '\n' || c == '|' || c == '&' ||
        c == ';' || c == '(' || c == ')' || c == '<' || c == '>') {
        return true;
    }
    return false;
}

static struct wl_str wl_scan_copy(struct wl_scanner *scan, size_t start,
                                  size_t end) {
    struct wl_str text;

    text.data = wl_arena_str(scan->lexer->arena, scan->lexer->src + start,
                            end - start);
    text.len = end - start;
    return text;
}

static bool wl_scan_single_quote(struct wl_scanner *scan) {
    struct wl_lexer *lexer = scan->lexer;

    wl_lex_skip(lexer, 1);
    while (!wl_lex_done(lexer) && wl_lex_peek_at(lexer, 0) != '\'') {
        if (!wl_scan_append_byte(scan, wl_lex_peek_at(lexer, 0), 'q')) {
            return false;
        }
        wl_lex_skip(lexer, 1);
    }
    if (wl_lex_done(lexer)) {
        return wl_scan_fail(scan, true, "unterminated single quote");
    }
    wl_lex_skip(lexer, 1);
    return true;
}

static bool wl_scan_dollar(struct wl_scanner *scan, bool quoted);

static bool wl_scan_backtick(struct wl_scanner *scan, bool quoted) {
    struct wl_lexer *lexer = scan->lexer;
    struct wl_word_part part;
    size_t inner;
    size_t end;

    wl_lex_skip(lexer, 1);
    inner = lexer->pos;
    while (!wl_lex_done(lexer)) {
        char c = wl_lex_peek_at(lexer, 0);

        if (c == '\\') {
            wl_lex_skip(lexer, 1);
            if (!wl_lex_done(lexer)) {
                wl_lex_skip(lexer, 1);
            }
            continue;
        }
        if (c == '`') {
            break;
        }
        wl_lex_skip(lexer, 1);
    }
    if (wl_lex_done(lexer)) {
        return wl_scan_fail(scan, true, "unterminated backquote");
    }
    end = lexer->pos;
    wl_lex_skip(lexer, 1);

    memset(&part, 0, sizeof part);
    part.kind = WL_WP_CMDSUB;
    part.quoted = quoted;
    part.u.cmdsub.source = wl_scan_copy(scan, inner, end);
    if (part.u.cmdsub.source.data == NULL) {
        return wl_scan_fail(scan, false, "out of memory");
    }
    part.u.cmdsub.program = NULL;
    return wl_scan_push(scan, &part);
}

static bool wl_scan_double_quote(struct wl_scanner *scan) {
    struct wl_lexer *lexer = scan->lexer;

    wl_lex_skip(lexer, 1);
    while (!wl_lex_done(lexer)) {
        char c = wl_lex_peek_at(lexer, 0);

        if (c == '"') {
            wl_lex_skip(lexer, 1);
            return true;
        }
        if (c == '\\') {
            char next;

            wl_lex_skip(lexer, 1);
            if (wl_lex_done(lexer)) {
                return wl_scan_fail(scan, true, "unterminated double quote");
            }
            next = wl_lex_peek_at(lexer, 0);
            if (next == '$' || next == '`' || next == '"' || next == '\\') {
                if (!wl_scan_append_byte(scan, next, 'q')) {
                    return false;
                }
                wl_lex_skip(lexer, 1);
            } else if (next == '\n') {
                wl_lex_skip(lexer, 1);
            } else {
                if (!wl_scan_append_byte(scan, '\\', 'q') ||
                    !wl_scan_append_byte(scan, next, 'q')) {
                    return false;
                }
                wl_lex_skip(lexer, 1);
            }
            continue;
        }
        if (c == '$') {
            if (!wl_scan_dollar(scan, true)) {
                return false;
            }
            continue;
        }
        if (c == '`') {
            if (!wl_scan_backtick(scan, true)) {
                return false;
            }
            continue;
        }
        if (!wl_scan_append_byte(scan, c, 'q')) {
            return false;
        }
        wl_lex_skip(lexer, 1);
    }

    return wl_scan_fail(scan, true, "unterminated double quote");
}

static bool wl_scan_cmdsub(struct wl_scanner *scan, bool quoted) {
    struct wl_lexer *lexer = scan->lexer;
    struct wl_word_part part;
    size_t inner;
    size_t end;
    int depth = 1;

    wl_lex_skip(lexer, 1); /* ( after $ */
    inner = lexer->pos;

    while (!wl_lex_done(lexer)) {
        char c = wl_lex_peek_at(lexer, 0);

        if (c == '\\') {
            wl_lex_skip(lexer, 1);
            if (!wl_lex_done(lexer)) {
                wl_lex_skip(lexer, 1);
            }
            continue;
        }
        if (c == '\'') {
            wl_lex_skip(lexer, 1);
            while (!wl_lex_done(lexer) && wl_lex_peek_at(lexer, 0) != '\'') {
                wl_lex_skip(lexer, 1);
            }
            if (wl_lex_done(lexer)) {
                return wl_scan_fail(scan, true, "unterminated $(");
            }
            wl_lex_skip(lexer, 1);
            continue;
        }
        if (c == '"') {
            wl_lex_skip(lexer, 1);
            while (!wl_lex_done(lexer)) {
                char inner_char = wl_lex_peek_at(lexer, 0);

                if (inner_char == '\\') {
                    wl_lex_skip(lexer, 1);
                    if (!wl_lex_done(lexer)) {
                        wl_lex_skip(lexer, 1);
                    }
                    continue;
                }
                if (inner_char == '"') {
                    wl_lex_skip(lexer, 1);
                    break;
                }
                wl_lex_skip(lexer, 1);
            }
            if (wl_lex_done(lexer)) {
                return wl_scan_fail(scan, true, "unterminated $(");
            }
            continue;
        }
        if (c == '`') {
            wl_lex_skip(lexer, 1);
            while (!wl_lex_done(lexer)) {
                char inner_char = wl_lex_peek_at(lexer, 0);

                if (inner_char == '\\') {
                    wl_lex_skip(lexer, 1);
                    if (!wl_lex_done(lexer)) {
                        wl_lex_skip(lexer, 1);
                    }
                    continue;
                }
                if (inner_char == '`') {
                    wl_lex_skip(lexer, 1);
                    break;
                }
                wl_lex_skip(lexer, 1);
            }
            if (wl_lex_done(lexer)) {
                return wl_scan_fail(scan, true, "unterminated $(");
            }
            continue;
        }
        if (c == '$' && wl_lex_peek_at(lexer, 1) == '(') {
            depth++;
            wl_lex_skip(lexer, 2);
            continue;
        }
        if (c == '(') {
            depth++;
            wl_lex_skip(lexer, 1);
            continue;
        }
        if (c == ')') {
            depth--;
            wl_lex_skip(lexer, 1);
            if (depth == 0) {
                break;
            }
            continue;
        }
        wl_lex_skip(lexer, 1);
    }

    if (depth != 0) {
        return wl_scan_fail(scan, true, "unterminated $(");
    }

    end = lexer->pos - 1;
    memset(&part, 0, sizeof part);
    part.kind = WL_WP_CMDSUB;
    part.quoted = quoted;
    part.u.cmdsub.source = wl_scan_copy(scan, inner, end);
    if (part.u.cmdsub.source.data == NULL) {
        return wl_scan_fail(scan, false, "out of memory");
    }
    part.u.cmdsub.program = NULL;
    return wl_scan_push(scan, &part);
}

static bool wl_scan_arith(struct wl_scanner *scan, bool quoted) {
    struct wl_lexer *lexer = scan->lexer;
    struct wl_word_part part;
    size_t inner;
    size_t end;
    int depth = 0;

    wl_lex_skip(lexer, 2); /* (( after $ */
    inner = lexer->pos;

    while (!wl_lex_done(lexer)) {
        char c = wl_lex_peek_at(lexer, 0);

        if (c == '\\') {
            wl_lex_skip(lexer, 1);
            if (!wl_lex_done(lexer)) {
                wl_lex_skip(lexer, 1);
            }
            continue;
        }
        if (c == '\'') {
            wl_lex_skip(lexer, 1);
            while (!wl_lex_done(lexer) && wl_lex_peek_at(lexer, 0) != '\'') {
                wl_lex_skip(lexer, 1);
            }
            if (wl_lex_done(lexer)) {
                return wl_scan_fail(scan, true, "unterminated $((...))");
            }
            wl_lex_skip(lexer, 1);
            continue;
        }
        if (c == '(') {
            depth++;
            wl_lex_skip(lexer, 1);
            continue;
        }
        if (c == ')') {
            if (depth == 0) {
                if (wl_lex_peek_at(lexer, 1) == ')') {
                    break;
                }
                return wl_scan_fail(scan, false, "expected '))' to end $((...))");
            }
            depth--;
            wl_lex_skip(lexer, 1);
            continue;
        }
        wl_lex_skip(lexer, 1);
    }

    if (depth != 0 || wl_lex_done(lexer)) {
        return wl_scan_fail(scan, true, "unterminated $((...))");
    }

    end = lexer->pos;
    wl_lex_skip(lexer, 2);

    memset(&part, 0, sizeof part);
    part.kind = WL_WP_ARITH;
    part.quoted = quoted;
    part.u.arith.source = wl_scan_copy(scan, inner, end);
    if (part.u.arith.source.data == NULL) {
        return wl_scan_fail(scan, false, "out of memory");
    }
    return wl_scan_push(scan, &part);
}

static bool wl_scan_tilde(struct wl_scanner *scan) {
    struct wl_lexer *lexer = scan->lexer;
    struct wl_word_part part;
    size_t start = lexer->pos;

    wl_lex_skip(lexer, 1);
    while (!wl_lex_done(lexer)) {
        char c = wl_lex_peek_at(lexer, 0);

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
            wl_lex_skip(lexer, 1);
            continue;
        }
        break;
    }

    memset(&part, 0, sizeof part);
    part.kind = WL_WP_TILDE;
    part.quoted = false;
    part.u.tilde = wl_scan_copy(scan, start, lexer->pos);
    if (part.u.tilde.data == NULL) {
        return wl_scan_fail(scan, false, "out of memory");
    }
    return wl_scan_push(scan, &part);
}

static bool wl_scan_body(struct wl_scanner *scan);

static struct wl_word *wl_scan_finish(struct wl_scanner *scan, size_t start) {
    struct wl_lexer *lexer = scan->lexer;
    struct wl_word *word;

    if (!wl_scan_flush(scan, false)) {
        return NULL;
    }

    word = wl_arena_alloc(lexer->arena, sizeof *word);
    if (word == NULL) {
        return NULL;
    }
    word->span.start = start;
    word->span.end = lexer->pos;
    word->count = scan->parts.count;
    word->quoted = false;

    if (scan->parts.count > 0) {
        word->parts = wl_arena_calloc(lexer->arena, scan->parts.count,
                                      sizeof *word->parts);
        if (word->parts == NULL) {
            return NULL;
        }
        memcpy(word->parts, scan->parts.items,
               scan->parts.count * sizeof *word->parts);
        for (size_t index = 0; index < word->count; index++) {
            if (word->parts[index].quoted) {
                word->quoted = true;
            }
        }
    }
    return word;
}

static bool wl_scan_operand(struct wl_scanner *scan,
                            struct wl_word **operand) {
    struct wl_scanner nested;
    struct wl_word *word;
    size_t start = scan->lexer->pos;

    wl_scan_init(&nested, scan->lexer, '}', false);
    if (!wl_scan_body(&nested)) {
        wl_scan_free(&nested);
        return false;
    }
    if (wl_lex_peek_at(scan->lexer, 0) != '}') {
        wl_scan_free(&nested);
        return wl_scan_fail(scan, wl_lex_done(scan->lexer),
                            "expected '}' to close ${...}");
    }
    word = wl_scan_finish(&nested, start);
    wl_scan_free(&nested);
    if (word == NULL) {
        return wl_scan_fail(scan, false, "out of memory");
    }
    *operand = word;
    return true;
}

static bool wl_scan_braced(struct wl_scanner *scan, bool quoted) {
    struct wl_lexer *lexer = scan->lexer;
    struct wl_word_part part;
    bool length = false;
    char c;

    memset(&part, 0, sizeof part);
    part.kind = WL_WP_PARAM;
    part.quoted = quoted;
    part.u.param.index = -1;
    part.u.param.special = 0;
    part.u.param.op = WL_PARAM_NONE;

    wl_lex_skip(lexer, 1); /* { */

    c = wl_lex_peek_at(lexer, 0);
    if (c == '#' && (wl_is_name_start(wl_lex_peek_at(lexer, 1)) ||
                     (wl_lex_peek_at(lexer, 1) >= '0' &&
                      wl_lex_peek_at(lexer, 1) <= '9') ||
                     wl_is_special(wl_lex_peek_at(lexer, 1)))) {
        length = true;
        wl_lex_skip(lexer, 1);
        c = wl_lex_peek_at(lexer, 0);
    }

    if (wl_is_name_start(c)) {
        size_t start = lexer->pos;

        while (!wl_lex_done(lexer) &&
               wl_is_name_char(wl_lex_peek_at(lexer, 0))) {
            wl_lex_skip(lexer, 1);
        }
        part.u.param.name = wl_scan_copy(scan, start, lexer->pos);
        if (part.u.param.name.data == NULL) {
            return wl_scan_fail(scan, false, "out of memory");
        }
    } else if (c >= '0' && c <= '9') {
        int index = 0;

        while (!wl_lex_done(lexer) &&
               wl_lex_peek_at(lexer, 0) >= '0' &&
               wl_lex_peek_at(lexer, 0) <= '9') {
            index = index * 10 + (wl_lex_peek_at(lexer, 0) - '0');
            if (index > 9999) {
                index = 9999;
            }
            wl_lex_skip(lexer, 1);
        }
        part.u.param.index = index;
    } else if (wl_is_special(c)) {
        part.u.param.special = c;
        wl_lex_skip(lexer, 1);
    } else {
        return wl_scan_fail(scan, false, "bad substitution");
    }

    if (length) {
        part.u.param.op = WL_PARAM_LENGTH;
        if (wl_lex_peek_at(lexer, 0) != '}') {
            return wl_scan_fail(scan, false, "bad substitution");
        }
    } else {
        char op_char = wl_lex_peek_at(lexer, 0);
        bool colon = false;
        bool needs_operand = false;

        if (op_char == ':') {
            colon = true;
            wl_lex_skip(lexer, 1);
            op_char = wl_lex_peek_at(lexer, 0);
        }

        switch (op_char) {
        case '-':
            part.u.param.op = WL_PARAM_DEFAULT;
            needs_operand = true;
            wl_lex_skip(lexer, 1);
            break;
        case '=':
            part.u.param.op = WL_PARAM_ASSIGN;
            needs_operand = true;
            wl_lex_skip(lexer, 1);
            break;
        case '?':
            part.u.param.op = WL_PARAM_ERROR;
            needs_operand = true;
            wl_lex_skip(lexer, 1);
            break;
        case '+':
            part.u.param.op = WL_PARAM_ALTERNATE;
            needs_operand = true;
            wl_lex_skip(lexer, 1);
            break;
        case '#':
            if (colon) {
                return wl_scan_fail(scan, false, "bad substitution");
            }
            wl_lex_skip(lexer, 1);
            if (wl_lex_peek_at(lexer, 0) == '#') {
                part.u.param.op = WL_PARAM_TRIM_PREFIX_LONG;
                wl_lex_skip(lexer, 1);
            } else {
                part.u.param.op = WL_PARAM_TRIM_PREFIX;
            }
            needs_operand = true;
            break;
        case '%':
            if (colon) {
                return wl_scan_fail(scan, false, "bad substitution");
            }
            wl_lex_skip(lexer, 1);
            if (wl_lex_peek_at(lexer, 0) == '%') {
                part.u.param.op = WL_PARAM_TRIM_SUFFIX_LONG;
                wl_lex_skip(lexer, 1);
            } else {
                part.u.param.op = WL_PARAM_TRIM_SUFFIX;
            }
            needs_operand = true;
            break;
        default:
            if (colon) {
                return wl_scan_fail(scan, false, "bad substitution");
            }
            break;
        }
        part.u.param.colon = colon;

        if (needs_operand) {
            struct wl_word *operand = NULL;

            if (!wl_scan_operand(scan, &operand)) {
                return false;
            }
            part.u.param.word = operand;
        }
    }

    if (wl_lex_peek_at(lexer, 0) != '}') {
        return wl_scan_fail(scan, wl_lex_done(lexer),
                            "expected '}' to close ${...}");
    }
    wl_lex_skip(lexer, 1);
    return wl_scan_push(scan, &part);
}

static bool wl_scan_dollar(struct wl_scanner *scan, bool quoted) {
    struct wl_lexer *lexer = scan->lexer;
    struct wl_word_part part;
    char c;

    if (!wl_scan_flush(scan, false)) {
        return false;
    }
    wl_lex_skip(lexer, 1); /* $ */
    if (wl_lex_done(lexer)) {
        return wl_scan_append_byte(scan, '$', quoted ? 'q' : 'u');
    }

    c = wl_lex_peek_at(lexer, 0);
    if (c == '{') {
        return wl_scan_braced(scan, quoted);
    }
    if (c == '(') {
        if (wl_lex_peek_at(lexer, 1) == '(') {
            return wl_scan_arith(scan, quoted);
        }
        return wl_scan_cmdsub(scan, quoted);
    }

    memset(&part, 0, sizeof part);
    part.kind = WL_WP_PARAM;
    part.quoted = quoted;
    part.u.param.index = -1;
    part.u.param.special = 0;
    part.u.param.op = WL_PARAM_NONE;

    if (wl_is_name_start(c)) {
        size_t start = lexer->pos;

        while (!wl_lex_done(lexer) &&
               wl_is_name_char(wl_lex_peek_at(lexer, 0))) {
            wl_lex_skip(lexer, 1);
        }
        part.u.param.name = wl_scan_copy(scan, start, lexer->pos);
        if (part.u.param.name.data == NULL) {
            return wl_scan_fail(scan, false, "out of memory");
        }
    } else if (c >= '0' && c <= '9') {
        part.u.param.index = c - '0';
        wl_lex_skip(lexer, 1);
    } else if (wl_is_special(c)) {
        part.u.param.special = c;
        wl_lex_skip(lexer, 1);
    } else {
        /* A lone '$' is an ordinary character. */
        return wl_scan_append_byte(scan, '$', quoted ? 'q' : 'u');
    }

    return wl_scan_push(scan, &part);
}

static bool wl_scan_body(struct wl_scanner *scan) {
    struct wl_lexer *lexer = scan->lexer;

    while (!wl_lex_done(lexer)) {
        char c = wl_lex_peek_at(lexer, 0);

        if (wl_scan_delimiter(c, scan->stop, scan->heredoc)) {
            break;
        }

        if (scan->heredoc) {
            if (c == '\\') {
                char next;

                wl_lex_skip(lexer, 1);
                if (wl_lex_done(lexer)) {
                    if (!wl_scan_append_byte(scan, '\\', 'u')) {
                        return false;
                    }
                    break;
                }
                next = wl_lex_peek_at(lexer, 0);
                if (next == '$' || next == '`' || next == '\\') {
                    if (!wl_scan_append_byte(scan, next, 'u')) {
                        return false;
                    }
                    wl_lex_skip(lexer, 1);
                } else if (next == '\n') {
                    wl_lex_skip(lexer, 1);
                } else {
                    if (!wl_scan_append_byte(scan, '\\', 'u') ||
                        !wl_scan_append_byte(scan, next, 'u')) {
                        return false;
                    }
                    wl_lex_skip(lexer, 1);
                }
                continue;
            }
            if (c == '$') {
                if (!wl_scan_dollar(scan, false)) {
                    return false;
                }
                continue;
            }
            if (c == '`') {
                if (!wl_scan_backtick(scan, false)) {
                    return false;
                }
                continue;
            }
            if (!wl_scan_append_byte(scan, c, 'u')) {
                return false;
            }
            wl_lex_skip(lexer, 1);
            continue;
        }

        if (c == '\\') {
            char next;

            wl_lex_skip(lexer, 1);
            if (wl_lex_done(lexer)) {
                return wl_scan_fail(scan, true, "trailing backslash");
            }
            next = wl_lex_peek_at(lexer, 0);
            if (next == '\n') {
                wl_lex_skip(lexer, 1);
                continue;
            }
            if (!wl_scan_append_byte(scan, next, 'q')) {
                return false;
            }
            wl_lex_skip(lexer, 1);
            continue;
        }

        if (c == '\'') {
            size_t mark_parts = scan->parts.count;

            if (!wl_scan_single_quote(scan)) {
                return false;
            }
            if (scan->parts.count == mark_parts && scan->literal.len == 0 &&
                scan->literal_state == 0) {
                /* Preserve "" as an empty quoted field. */
                scan->literal_state = 'q';
                if (!wl_scan_flush(scan, true)) {
                    return false;
                }
            }
            continue;
        }

        if (c == '"') {
            size_t mark_parts = scan->parts.count;

            if (!wl_scan_double_quote(scan)) {
                return false;
            }
            if (scan->parts.count == mark_parts && scan->literal.len == 0 &&
                scan->literal_state == 0) {
                scan->literal_state = 'q';
                if (!wl_scan_flush(scan, true)) {
                    return false;
                }
            }
            continue;
        }

        if (c == '$') {
            if (!wl_scan_dollar(scan, false)) {
                return false;
            }
            continue;
        }

        if (c == '`') {
            if (!wl_scan_backtick(scan, false)) {
                return false;
            }
            continue;
        }

        if (c == '~' && scan->parts.count == 0 && scan->literal.len == 0 &&
            scan->literal_state == 0) {
            if (!wl_scan_tilde(scan)) {
                return false;
            }
            continue;
        }

        if (!wl_scan_append_byte(scan, c, 'u')) {
            return false;
        }
        wl_lex_skip(lexer, 1);
    }

    return true;
}

bool wl_lex_word(struct wl_lexer *lexer, struct wl_word **word) {
    struct wl_scanner scan;
    struct wl_word *result = NULL;
    size_t start = lexer->pos;
    bool ok;

    if (lexer->depth >= WL_LEX_MAX_DEPTH) {
        wl_diag_set(lexer->diag, lexer->pos, false,
                    "word nesting is too deep (max %d)", WL_LEX_MAX_DEPTH);
        return false;
    }

    wl_scan_init(&scan, lexer, 0, false);
    lexer->depth++;
    ok = wl_scan_body(&scan);
    if (ok) {
        result = wl_scan_finish(&scan, start);
        if (result == NULL) {
            wl_diag_set(lexer->diag, start, false, "out of memory");
            ok = false;
        }
    }
    lexer->depth--;
    wl_scan_free(&scan);

    if (!ok) {
        return false;
    }
    *word = result;
    return true;
}

bool wl_lex_word_heredoc(struct wl_lexer *lexer, struct wl_word **word) {
    struct wl_scanner scan;
    struct wl_word *result = NULL;
    size_t start = lexer->pos;
    bool ok;

    wl_scan_init(&scan, lexer, 0, true);
    ok = wl_scan_body(&scan);
    if (ok) {
        result = wl_scan_finish(&scan, start);
        if (result == NULL) {
            wl_diag_set(lexer->diag, start, false, "out of memory");
            ok = false;
        }
    }
    wl_scan_free(&scan);

    if (!ok) {
        return false;
    }
    *word = result;
    return true;
}
