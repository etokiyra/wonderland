#include "expand/arith.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/shell.h"

/*
 * Recursive-descent integer arithmetic. Small on purpose: it exists so that
 * `$((1 + 2))` works, not so that the shell has a calculator. Assignments and
 * comparisons are explicitly out of scope until they have tests of their own.
 */

struct parser {
    struct wl_shell *shell;
    const char *source;
    size_t length;
    size_t position;
    char *error;
    size_t error_size;
    bool failed;
};

static void fail(struct parser *parser, const char *message) {
    if (!parser->failed) {
        parser->failed = true;
        (void)snprintf(parser->error, parser->error_size, "%s", message);
    }
}

static void skip_space(struct parser *parser) {
    while (parser->position < parser->length &&
           (parser->source[parser->position] == ' ' ||
            parser->source[parser->position] == '\t' ||
            parser->source[parser->position] == '\n')) {
        parser->position++;
    }
}

static bool parse_expression(struct parser *parser, long long *out);

static long long variable_value(struct parser *parser, const char *name,
                                size_t name_len) {
    const char *text = wl_var_get(&parser->shell->vars, name, name_len);

    if (text == NULL) {
        return 0;
    }
    return strtoll(text, NULL, 0);
}

static bool parse_number(struct parser *parser, long long *out) {
    size_t start = parser->position;
    char buffer[64];
    size_t length;

    if (parser->position < parser->length &&
        parser->source[parser->position] == '0' &&
        parser->position + 1 < parser->length &&
        (parser->source[parser->position + 1] == 'x' ||
         parser->source[parser->position + 1] == 'X')) {
        parser->position += 2;
        while (parser->position < parser->length &&
               isxdigit((unsigned char)parser->source[parser->position])) {
            parser->position++;
        }
    } else {
        while (parser->position < parser->length &&
               isdigit((unsigned char)parser->source[parser->position])) {
            parser->position++;
        }
    }

    length = parser->position - start;
    if (length == 0 || length >= sizeof buffer) {
        fail(parser, "arithmetic: expected a number");
        return false;
    }
    memcpy(buffer, parser->source + start, length);
    buffer[length] = '\0';
    *out = strtoll(buffer, NULL, 0);
    return true;
}

static bool parse_primary(struct parser *parser, long long *out) {
    skip_space(parser);

    if (parser->position >= parser->length) {
        fail(parser, "arithmetic: unexpected end of expression");
        return false;
    }

    if (parser->source[parser->position] == '(') {
        parser->position++;
        if (!parse_expression(parser, out)) {
            return false;
        }
        skip_space(parser);
        if (parser->position >= parser->length ||
            parser->source[parser->position] != ')') {
            fail(parser, "arithmetic: expected ')'");
            return false;
        }
        parser->position++;
        return true;
    }

    if (isdigit((unsigned char)parser->source[parser->position])) {
        return parse_number(parser, out);
    }

    if (parser->source[parser->position] == '$') {
        parser->position++;
        if (parser->position < parser->length &&
            isdigit((unsigned char)parser->source[parser->position])) {
            long long index = 0;
            const struct wl_frame *frame = parser->shell->frame;

            while (parser->position < parser->length &&
                   isdigit((unsigned char)parser->source[parser->position])) {
                index = index * 10 + (parser->source[parser->position] - '0');
                if (index > 9999) {
                    index = 9999;
                }
                parser->position++;
            }
            if (frame != NULL && (size_t)index <= frame->count) {
                *out = strtoll(frame->args[index - 1], NULL, 0);
            } else {
                *out = 0;
            }
            return true;
        }
    }

    if (isalpha((unsigned char)parser->source[parser->position]) ||
        parser->source[parser->position] == '_') {
        size_t start = parser->position;

        while (parser->position < parser->length &&
               (isalnum((unsigned char)parser->source[parser->position]) ||
                parser->source[parser->position] == '_')) {
            parser->position++;
        }
        *out = variable_value(parser, parser->source + start,
                              parser->position - start);
        return true;
    }

    fail(parser, "arithmetic: expected a value");
    return false;
}

static bool parse_unary(struct parser *parser, long long *out) {
    skip_space(parser);

    if (parser->position < parser->length) {
        char c = parser->source[parser->position];

        if (c == '+' || c == '-') {
            parser->position++;
            if (!parse_unary(parser, out)) {
                return false;
            }
            if (c == '-') {
                *out = -*out;
            }
            return true;
        }
    }
    return parse_primary(parser, out);
}

static bool parse_term(struct parser *parser, long long *out) {
    long long left;

    if (!parse_unary(parser, &left)) {
        return false;
    }
    for (;;) {
        char operator_char;

        skip_space(parser);
        if (parser->position >= parser->length) {
            break;
        }
        operator_char = parser->source[parser->position];
        if (operator_char != '*' && operator_char != '/' &&
            operator_char != '%') {
            break;
        }
        parser->position++;

        {
            long long right;

            if (!parse_unary(parser, &right)) {
                return false;
            }
            if ((operator_char == '/' || operator_char == '%') && right == 0) {
                fail(parser, "arithmetic: division by zero");
                return false;
            }
            switch (operator_char) {
            case '*':
                left *= right;
                break;
            case '/':
                left /= right;
                break;
            default:
                left %= right;
                break;
            }
        }
    }
    *out = left;
    return true;
}

static bool parse_expression(struct parser *parser, long long *out) {
    long long left;

    if (!parse_term(parser, &left)) {
        return false;
    }
    for (;;) {
        char operator_char;

        skip_space(parser);
        if (parser->position >= parser->length) {
            break;
        }
        operator_char = parser->source[parser->position];
        if (operator_char != '+' && operator_char != '-') {
            break;
        }
        parser->position++;

        {
            long long right;

            if (!parse_term(parser, &right)) {
                return false;
            }
            left = (operator_char == '+') ? left + right : left - right;
        }
    }
    *out = left;
    return true;
}

bool wl_arith_eval(struct wl_shell *shell, const char *source, size_t length,
                   long long *value, char *error, size_t error_size) {
    struct parser parser;
    long long result = 0;

    memset(&parser, 0, sizeof parser);
    parser.shell = shell;
    parser.source = source;
    parser.length = length;
    parser.error = error;
    parser.error_size = error_size;

    if (!parse_expression(&parser, &result)) {
        return false;
    }
    skip_space(&parser);
    if (parser.position != parser.length) {
        fail(&parser, "arithmetic: unexpected trailing input");
        return false;
    }
    *value = result;
    return true;
}
