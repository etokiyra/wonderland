#include "ast/ast.h"

#include "ast/arena.h"

#include <string.h>

struct wl_node *wl_ast_node(struct wl_arena *arena, enum wl_node_kind kind,
                            struct wl_span span) {
    struct wl_node *node = wl_arena_alloc(arena, sizeof *node);

    if (node == NULL) {
        return NULL;
    }
    node->kind = kind;
    node->span = span;
    return node;
}

bool wl_word_literal(const struct wl_word *word, struct wl_buf *out) {
    if (word == NULL) {
        return true;
    }
    for (size_t index = 0; index < word->count; index++) {
        const struct wl_word_part *part = &word->parts[index];

        if (part->kind != WL_WP_LITERAL) {
            return false;
        }
        if (!wl_buf_append(out, part->u.literal.data, part->u.literal.len)) {
            return false;
        }
    }
    return true;
}

static bool wl_is_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool wl_is_name_char(char c) {
    return wl_is_name_start(c) || (c >= '0' && c <= '9');
}

bool wl_word_split_assignment(struct wl_arena *arena,
                              const struct wl_word *word,
                              struct wl_str *name, struct wl_word **value) {
    const struct wl_word_part *first;
    size_t equal = 0;
    size_t name_length;
    struct wl_word *result;
    size_t first_value_part;
    size_t value_parts;

    if (word == NULL || word->count == 0 || name == NULL || value == NULL) {
        return false;
    }
    first = &word->parts[0];
    if (first->kind != WL_WP_LITERAL || first->quoted) {
        return false;
    }
    while (equal < first->u.literal.len &&
           first->u.literal.data[equal] != '=') {
        equal++;
    }
    if (equal == 0 || equal == first->u.literal.len) {
        return false;
    }
    if (!wl_is_name_start(first->u.literal.data[0])) {
        return false;
    }
    for (size_t index = 1; index < equal; index++) {
        if (!wl_is_name_char(first->u.literal.data[index])) {
            return false;
        }
    }

    name_length = equal;
    name->data = wl_arena_str(arena, first->u.literal.data, name_length);
    name->len = name_length;
    if (name->data == NULL) {
        return false;
    }

    /* Value = the rest of the first part (if any) plus every later part. */
    first_value_part = (equal + 1 < first->u.literal.len) ? 1 : 0;
    value_parts = word->count - (first_value_part ? 0 : 1);

    result = wl_arena_alloc(arena, sizeof *result);
    if (result == NULL) {
        return false;
    }
    result->span = word->span;
    result->quoted = word->quoted;
    result->count = value_parts;

    if (value_parts > 0) {
        result->parts = wl_arena_calloc(arena, value_parts, sizeof *result->parts);
        if (result->parts == NULL) {
            return false;
        }
    } else {
        result->parts = NULL;
    }

    if (first_value_part) {
        struct wl_word_part *part = &result->parts[0];

        *part = first[0];
        part->u.literal.data = first->u.literal.data + equal + 1;
        part->u.literal.len = first->u.literal.len - equal - 1;
        for (size_t index = 1; index < value_parts; index++) {
            result->parts[index] = word->parts[index];
        }
    } else if (value_parts > 0) {
        for (size_t index = 0; index < value_parts; index++) {
            result->parts[index] = word->parts[index + 1];
        }
    }

    *value = result;
    return true;
}
