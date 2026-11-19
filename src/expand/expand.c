#include "expand/expand.h"

#include <errno.h>
#include <fnmatch.h>
#include <glob.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/shell.h"
#include "core/var.h"
#include "expand/arith.h"

#define WL_EXPAND_MAX_DEPTH 64

/*
 * Expansion works on bytes with two independent flags per byte:
 *
 *   quoted  byte came from quoting or a quoted expansion: never globbed
 *   split   byte came from an *unquoted* expansion: may delimit IFS fields
 *
 * Keeping both is what makes `a:b` with IFS=: one field while `$x` with the
 * same value splits: field splitting applies to expansion results, not to
 * literal text. It also lets globbing see exactly which metacharacters were
 * quoted.
 */

struct wl_eb {
    char c;
    unsigned char quoted;
    unsigned char split;
};

struct wl_ebuf {
    struct wl_eb *items;
    size_t len;
    size_t capacity;
};

static void ebuf_init(struct wl_ebuf *buffer) {
    buffer->items = NULL;
    buffer->len = 0;
    buffer->capacity = 0;
}

static void ebuf_free(struct wl_ebuf *buffer) {
    free(buffer->items);
    buffer->items = NULL;
    buffer->len = 0;
    buffer->capacity = 0;
}

static bool fail(struct wl_expand_ctx *ctx, const char *format, ...) {
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(ctx->error, sizeof ctx->error, format, arguments);
    va_end(arguments);
    return false;
}

static bool ebuf_push(struct wl_expand_ctx *ctx, struct wl_ebuf *buffer,
                      char c, bool quoted, bool split) {
    if (buffer->len == buffer->capacity) {
        size_t capacity = (buffer->capacity > 0) ? buffer->capacity * 2 : 32;
        struct wl_eb *grown = realloc(buffer->items, capacity * sizeof *grown);

        if (grown == NULL) {
            return fail(ctx, "out of memory");
        }
        buffer->items = grown;
        buffer->capacity = capacity;
    }
    buffer->items[buffer->len].c = c;
    buffer->items[buffer->len].quoted = quoted ? 1 : 0;
    buffer->items[buffer->len].split = split ? 1 : 0;
    buffer->len++;
    return true;
}

static bool ebuf_push_text(struct wl_expand_ctx *ctx, struct wl_ebuf *buffer,
                           const char *data, size_t len, bool quoted,
                           bool split) {
    for (size_t index = 0; index < len; index++) {
        if (!ebuf_push(ctx, buffer, data[index], quoted, split)) {
            return false;
        }
    }
    return true;
}

void wl_argv_init(struct wl_argv *argv) {
    argv->items = NULL;
    argv->count = 0;
    argv->capacity = 0;
}

void wl_argv_free(struct wl_argv *argv) {
    for (size_t index = 0; index < argv->count; index++) {
        free(argv->items[index]);
    }
    free(argv->items);
    argv->items = NULL;
    argv->count = 0;
    argv->capacity = 0;
}

bool wl_argv_push(struct wl_argv *argv, char *owned) {
    if (argv->count + 1 >= argv->capacity) {
        size_t capacity = (argv->capacity > 0) ? argv->capacity * 2 : 8;
        char **grown;

        while (capacity < argv->count + 2) {
            capacity *= 2;
        }
        grown = realloc(argv->items, capacity * sizeof *grown);
        if (grown == NULL) {
            return false;
        }
        argv->items = grown;
        argv->capacity = capacity;
    }
    argv->items[argv->count++] = owned;
    argv->items[argv->count] = NULL;
    return true;
}

static bool expand_word_bytes(struct wl_expand_ctx *ctx,
                              const struct wl_word *word,
                              struct wl_ebuf *out, int depth);

static bool expand_single_depth(struct wl_expand_ctx *ctx,
                                const struct wl_word *word,
                                struct wl_buf *out, int depth);

static bool tilde_value(struct wl_expand_ctx *ctx,
                        const struct wl_str *tilde, struct wl_buf *out) {
    if (tilde->len == 1) {
        const char *home = wl_var_get(&ctx->shell->vars, "HOME", 4);

        if (home == NULL) {
            home = "";
        }
        return wl_buf_append_str(out, home);
    }

    {
        char name[256];
        struct passwd *entry;

        if (tilde->len - 1 >= sizeof name) {
            return wl_buf_append(out, tilde->data, tilde->len);
        }
        memcpy(name, tilde->data + 1, tilde->len - 1);
        name[tilde->len - 1] = '\0';

        entry = getpwnam(name);
        if (entry == NULL || entry->pw_dir == NULL) {
            /* POSIX leaves an unknown ~user unchanged. */
            return wl_buf_append(out, tilde->data, tilde->len);
        }
        return wl_buf_append_str(out, entry->pw_dir);
    }
}

static bool match_text(const char *pattern, size_t pattern_len,
                       const char *text, size_t text_len) {
    char *pattern_copy = malloc(pattern_len + 1);
    char *text_copy = malloc(text_len + 1);
    int result;

    if (pattern_copy == NULL || text_copy == NULL) {
        free(pattern_copy);
        free(text_copy);
        return false;
    }
    if (pattern_len > 0) {
        memcpy(pattern_copy, pattern, pattern_len);
    }
    pattern_copy[pattern_len] = '\0';
    if (text_len > 0) {
        memcpy(text_copy, text, text_len);
    }
    text_copy[text_len] = '\0';
    result = fnmatch(pattern_copy, text_copy, 0);
    free(pattern_copy);
    free(text_copy);
    return result == 0;
}

static bool trim_value(struct wl_expand_ctx *ctx,
                       const struct wl_param *param, const char *value,
                       size_t value_len, struct wl_buf *out, int depth) {
    struct wl_buf pattern;
    const char *pattern_data;
    size_t pattern_len;
    bool ok = false;

    wl_buf_init(&pattern);
    if (param->word != NULL &&
        !expand_single_depth(ctx, param->word, &pattern, depth + 1)) {
        wl_buf_free(&pattern);
        return false;
    }
    pattern_data = (pattern.data != NULL) ? pattern.data : "";
    pattern_len = pattern.len;

    switch (param->op) {
    case WL_PARAM_TRIM_PREFIX:
        for (size_t cut = 0; cut <= value_len; cut++) {
            if (match_text(pattern_data, pattern_len, value, cut)) {
                ok = wl_buf_append(out, value + cut, value_len - cut);
                break;
            }
        }
        break;
    case WL_PARAM_TRIM_PREFIX_LONG:
        for (size_t cut = value_len + 1; cut-- > 0;) {
            if (match_text(pattern_data, pattern_len, value, cut)) {
                ok = wl_buf_append(out, value + cut, value_len - cut);
                break;
            }
        }
        break;
    case WL_PARAM_TRIM_SUFFIX:
        for (size_t cut = value_len + 1; cut-- > 0;) {
            if (match_text(pattern_data, pattern_len, value + cut,
                           value_len - cut)) {
                ok = wl_buf_append(out, value, cut);
                break;
            }
        }
        break;
    case WL_PARAM_TRIM_SUFFIX_LONG:
        for (size_t cut = 0; cut <= value_len; cut++) {
            if (match_text(pattern_data, pattern_len, value + cut,
                           value_len - cut)) {
                ok = wl_buf_append(out, value, cut);
                break;
            }
        }
        break;
    default:
        ok = wl_buf_append(out, value, value_len);
        break;
    }

    wl_buf_free(&pattern);
    if (!ok && ctx->error[0] == '\0') {
        (void)fail(ctx, "out of memory");
    }
    return ok;
}

static bool append_number(struct wl_expand_ctx *ctx, struct wl_buf *out,
                          long long number) {
    char buffer[32];

    (void)ctx;
    (void)snprintf(buffer, sizeof buffer, "%lld", number);
    return wl_buf_append_str(out, buffer);
}

static bool special_param(struct wl_expand_ctx *ctx,
                          const struct wl_param *param, struct wl_buf *out,
                          bool *is_set) {
    *is_set = true;

    switch (param->special) {
    case '?':
        return append_number(ctx, out, ctx->shell->last_status);
    case '$':
        return append_number(ctx, out, (long long)getpid());
    case '#': /* positional parameters: none yet */
        return wl_buf_append_str(out, "0");
    case '@':
    case '*':
    case '-':
        return true;
    case '!':
        *is_set = false;
        return true;
    default:
        *is_set = false;
        return true;
    }
}

static bool ebuf_text(struct wl_expand_ctx *ctx, const struct wl_ebuf *buffer,
                      struct wl_buf *out) {
    for (size_t index = 0; index < buffer->len; index++) {
        if (!wl_buf_append_char(out, buffer->items[index].c)) {
            return fail(ctx, "out of memory");
        }
    }
    return true;
}

static bool overlay(struct wl_expand_ctx *ctx, struct wl_ebuf *out,
                    const struct wl_ebuf *temp, bool outer_quoted) {
    for (size_t index = 0; index < temp->len; index++) {
        bool quoted = outer_quoted || temp->items[index].quoted != 0;

        if (!ebuf_push(ctx, out, temp->items[index].c, quoted, !quoted)) {
            return false;
        }
    }
    return true;
}

/*
 * Appends one parameter expansion to the word buffer.
 *
 * The quoting overlay is the subtle part: `${x:-a b}` is split when unquoted,
 * while `${x:-"a b"}` is not. The operand keeps its own per-byte quoting, and
 * the outer quoting is applied on top; every resulting byte splits exactly
 * when it is not quoted. This matches dash and bash (verified by test).
 */
static bool expand_param_to(struct wl_expand_ctx *ctx,
                            const struct wl_param *param, bool outer_quoted,
                            struct wl_ebuf *out, int depth) {
    struct wl_buf value;
    struct wl_ebuf temp;
    bool is_set = false;
    bool is_null;
    const char *text = NULL;
    size_t text_len = 0;
    char display[128];
    bool ok = true;

    wl_buf_init(&value);
    ebuf_init(&temp);

    if (param->name.len > 0) {
        text = wl_var_get(&ctx->shell->vars, param->name.data,
                          param->name.len);
        if (text != NULL) {
            is_set = true;
            if (!wl_buf_append_str(&value, text)) {
                wl_buf_free(&value);
                ebuf_free(&temp);
                return fail(ctx, "out of memory");
            }
            text_len = strlen(text);
        }
        (void)snprintf(display, sizeof display, "%.*s", (int)param->name.len,
                       param->name.data);
    } else if (param->special != 0) {
        if (!special_param(ctx, param, &value, &is_set)) {
            wl_buf_free(&value);
            ebuf_free(&temp);
            return false;
        }
        text_len = value.len;
        (void)snprintf(display, sizeof display, "%c", param->special);
    } else if (param->index == 0) {
        is_set = true;
        if (!wl_buf_append_str(&value, "wonderland")) {
            wl_buf_free(&value);
            ebuf_free(&temp);
            return fail(ctx, "out of memory");
        }
        text_len = value.len;
        (void)snprintf(display, sizeof display, "0");
    } else {
        (void)snprintf(display, sizeof display, "%d", param->index);
    }

    is_null = !is_set || value.len == 0;

    if (param->op == WL_PARAM_NONE) {
        ok = ebuf_push_text(ctx, &temp, value.data, value.len, false, false);
    } else if (param->op == WL_PARAM_LENGTH) {
        char number[32];

        (void)snprintf(number, sizeof number, "%zu", text_len);
        ok = ebuf_push_text(ctx, &temp, number, strlen(number), false, false);
    } else if (param->op == WL_PARAM_TRIM_PREFIX ||
               param->op == WL_PARAM_TRIM_PREFIX_LONG ||
               param->op == WL_PARAM_TRIM_SUFFIX ||
               param->op == WL_PARAM_TRIM_SUFFIX_LONG) {
        if (is_set) {
            struct wl_buf trimmed;

            wl_buf_init(&trimmed);
            ok = trim_value(ctx, param, value.data, value.len, &trimmed,
                            depth);
            if (ok) {
                ok = ebuf_push_text(ctx, &temp, trimmed.data, trimmed.len,
                                    false, false);
            }
            wl_buf_free(&trimmed);
        }
    } else {
        bool take_operand = param->colon ? is_null : !is_set;

        if (param->op == WL_PARAM_ALTERNATE) {
            take_operand = !take_operand;
        }
        if (take_operand) {
            if (param->word != NULL) {
                ok = expand_word_bytes(ctx, param->word, &temp, depth + 1);
            }
        } else {
            ok = ebuf_push_text(ctx, &temp, value.data, value.len, false,
                                false);
        }

        if (ok) {
            struct wl_buf operand_text;

            wl_buf_init(&operand_text);
            ok = ebuf_text(ctx, &temp, &operand_text);
            if (ok && param->op == WL_PARAM_ASSIGN && take_operand &&
                param->name.len > 0) {
                if (!wl_var_set(&ctx->shell->vars, param->name.data,
                                param->name.len,
                                (operand_text.data != NULL)
                                    ? operand_text.data
                                    : "",
                                operand_text.len, false)) {
                    (void)fail(ctx, "out of memory");
                    ok = false;
                }
            }
            if (ok && param->op == WL_PARAM_ERROR && take_operand) {
                const char *message = (operand_text.len > 0)
                                          ? operand_text.data
                                          : "parameter null or not set";

                (void)fail(ctx, "%s: %s", display, message);
                ok = false;
            }
            wl_buf_free(&operand_text);
        }
    }

    if (ok) {
        ok = overlay(ctx, out, &temp, outer_quoted);
    }
    ebuf_free(&temp);
    wl_buf_free(&value);
    return ok;
}

static bool expand_cmdsub(struct wl_expand_ctx *ctx,
                          const struct wl_word_part *part,
                          struct wl_buf *out) {
    struct wl_buf captured;
    int status = 0;
    size_t length;

    if (ctx->capture == NULL || part->u.cmdsub.program == NULL) {
        return fail(ctx, "command substitution is not available");
    }
    wl_buf_init(&captured);
    if (!ctx->capture(ctx->capture_context, part->u.cmdsub.program, &captured,
                      &status)) {
        wl_buf_free(&captured);
        return fail(ctx, "command substitution failed");
    }

    length = captured.len;
    while (length > 0 && captured.data[length - 1] == '\n') {
        length--;
    }
    if (!wl_buf_append(out, captured.data, length)) {
        wl_buf_free(&captured);
        return fail(ctx, "out of memory");
    }
    wl_buf_free(&captured);
    return true;
}

static bool expand_word_bytes(struct wl_expand_ctx *ctx,
                              const struct wl_word *word,
                              struct wl_ebuf *out, int depth) {
    if (depth > WL_EXPAND_MAX_DEPTH) {
        return fail(ctx, "expansion nesting is too deep");
    }

    for (size_t index = 0; index < word->count; index++) {
        const struct wl_word_part *part = &word->parts[index];

        switch (part->kind) {
        case WL_WP_LITERAL:
            if (!ebuf_push_text(ctx, out, part->u.literal.data,
                                part->u.literal.len, part->quoted, false)) {
                return false;
            }
            break;

        case WL_WP_TILDE: {
            struct wl_buf value;

            wl_buf_init(&value);
            if (!tilde_value(ctx, &part->u.tilde, &value)) {
                wl_buf_free(&value);
                return fail(ctx, "out of memory");
            }
            if (!ebuf_push_text(ctx, out, value.data, value.len, true, false)) {
                wl_buf_free(&value);
                return false;
            }
            wl_buf_free(&value);
            break;
        }

        case WL_WP_PARAM:
            if (!expand_param_to(ctx, &part->u.param, part->quoted, out,
                                 depth)) {
                return false;
            }
            break;

        case WL_WP_CMDSUB: {
            struct wl_buf value;

            wl_buf_init(&value);
            if (!expand_cmdsub(ctx, part, &value)) {
                wl_buf_free(&value);
                return false;
            }
            if (!ebuf_push_text(ctx, out, value.data, value.len,
                                part->quoted, !part->quoted)) {
                wl_buf_free(&value);
                return false;
            }
            wl_buf_free(&value);
            break;
        }

        case WL_WP_ARITH: {
            struct wl_buf value;
            long long number = 0;
            char error[160];

            error[0] = '\0';
            if (!wl_arith_eval(ctx->shell, part->u.arith.source.data,
                               part->u.arith.source.len, &number, error,
                               sizeof error)) {
                return fail(ctx, "%s", error);
            }
            wl_buf_init(&value);
            if (!append_number(ctx, &value, number)) {
                wl_buf_free(&value);
                return false;
            }
            if (!ebuf_push_text(ctx, out, value.data, value.len,
                                part->quoted, !part->quoted)) {
                wl_buf_free(&value);
                return false;
            }
            wl_buf_free(&value);
            break;
        }
        }
    }
    return true;
}

static bool expand_single_depth(struct wl_expand_ctx *ctx,
                                const struct wl_word *word,
                                struct wl_buf *out, int depth) {
    struct wl_ebuf bytes;
    bool ok = true;

    ebuf_init(&bytes);
    if (!expand_word_bytes(ctx, word, &bytes, depth)) {
        ebuf_free(&bytes);
        return false;
    }
    for (size_t index = 0; index < bytes.len; index++) {
        if (!wl_buf_append_char(out, bytes.items[index].c)) {
            ok = fail(ctx, "out of memory");
            break;
        }
    }
    ebuf_free(&bytes);
    return ok;
}

bool wl_expand_single(struct wl_expand_ctx *ctx, const struct wl_word *word,
                      struct wl_buf *out) {
    return expand_single_depth(ctx, word, out, 0);
}

static bool plain_field(struct wl_expand_ctx *ctx, const struct wl_eb *items,
                        size_t len, struct wl_argv *out) {
    char *copy = malloc(len + 1);

    if (copy == NULL) {
        return fail(ctx, "out of memory");
    }
    for (size_t index = 0; index < len; index++) {
        copy[index] = items[index].c;
    }
    copy[len] = '\0';
    if (!wl_argv_push(out, copy)) {
        free(copy);
        return fail(ctx, "out of memory");
    }
    return true;
}

static bool emit_field(struct wl_expand_ctx *ctx, const struct wl_eb *items,
                       size_t len, bool force_empty, struct wl_argv *out) {
    bool glob_needed = false;
    struct wl_buf pattern;
    glob_t matches;
    int result;

    if (len == 0) {
        return force_empty ? plain_field(ctx, items, 0, out) : true;
    }

    for (size_t index = 0; index < len; index++) {
        if (!items[index].quoted && (items[index].c == '*' ||
                                     items[index].c == '?' ||
                                     items[index].c == '[')) {
            glob_needed = true;
            break;
        }
    }
    if (!glob_needed) {
        return plain_field(ctx, items, len, out);
    }

    wl_buf_init(&pattern);
    for (size_t index = 0; index < len; index++) {
        char c = items[index].c;

        if (items[index].quoted &&
            (c == '*' || c == '?' || c == '[' || c == ']' || c == '\\')) {
            if (!wl_buf_append_char(&pattern, '\\')) {
                wl_buf_free(&pattern);
                return fail(ctx, "out of memory");
            }
        }
        if (!wl_buf_append_char(&pattern, c)) {
            wl_buf_free(&pattern);
            return fail(ctx, "out of memory");
        }
    }

    memset(&matches, 0, sizeof matches);
    result = glob(pattern.data, 0, NULL, &matches);
    wl_buf_free(&pattern);

    if (result == 0) {
        bool ok = true;

        for (size_t index = 0; index < matches.gl_pathc; index++) {
            char *copy = malloc(strlen(matches.gl_pathv[index]) + 1);

            if (copy == NULL) {
                ok = fail(ctx, "out of memory");
                break;
            }
            strcpy(copy, matches.gl_pathv[index]);
            if (!wl_argv_push(out, copy)) {
                free(copy);
                ok = fail(ctx, "out of memory");
                break;
            }
        }
        globfree(&matches);
        return ok;
    }

    /* No match, allocation failure, or read error: the pattern is literal. */
    return plain_field(ctx, items, len, out);
}

static bool expand_word_fields(struct wl_expand_ctx *ctx,
                               const struct wl_word *word,
                               struct wl_argv *out, int depth) {
    struct wl_ebuf bytes;
    const char *ifs;
    bool ok = true;

    ebuf_init(&bytes);
    if (!expand_word_bytes(ctx, word, &bytes, depth)) {
        ebuf_free(&bytes);
        return false;
    }

    ifs = wl_var_get(&ctx->shell->vars, "IFS", 3);
    if (ifs == NULL) {
        ifs = " \t\n";
    }

    if (bytes.len == 0) {
        if (word->quoted) {
            ok = plain_field(ctx, bytes.items, 0, out);
        }
        ebuf_free(&bytes);
        return ok;
    }

    if (ifs[0] == '\0') {
        ok = emit_field(ctx, bytes.items, bytes.len, false, out);
        ebuf_free(&bytes);
        return ok;
    }

    {
        size_t start = 0;

        for (size_t index = 0; index < bytes.len; index++) {
            bool delimiter = bytes.items[index].split &&
                             bytes.items[index].c != '\0' &&
                             strchr(ifs, bytes.items[index].c) != NULL;

            if (!delimiter) {
                continue;
            }
            if (index > start) {
                if (!emit_field(ctx, bytes.items + start, index - start, false,
                                out)) {
                    ok = false;
                    break;
                }
            }
            start = index + 1;
        }
        if (ok && bytes.len > start) {
            ok = emit_field(ctx, bytes.items + start, bytes.len - start, false,
                            out);
        }
    }

    ebuf_free(&bytes);
    return ok;
}

bool wl_expand_words(struct wl_expand_ctx *ctx, struct wl_word *const *words,
                     size_t count, struct wl_argv *out) {
    ctx->error[0] = '\0';
    for (size_t index = 0; index < count; index++) {
        if (!expand_word_fields(ctx, words[index], out, 0)) {
            return false;
        }
    }
    return true;
}

bool wl_expand_target(struct wl_expand_ctx *ctx, const struct wl_word *word,
                      char **out, bool *ambiguous) {
    struct wl_argv fields;

    wl_argv_init(&fields);
    ctx->error[0] = '\0';
    if (!expand_word_fields(ctx, word, &fields, 0)) {
        wl_argv_free(&fields);
        return false;
    }
    if (fields.count != 1) {
        *ambiguous = true;
        wl_argv_free(&fields);
        return true;
    }
    *ambiguous = false;
    *out = fields.items[0];
    fields.items[0] = NULL;
    wl_argv_free(&fields);
    return true;
}
