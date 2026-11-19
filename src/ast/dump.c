#include "ast/ast.h"

#include "ast/arena.h"
#include "core/buf.h"

#include <stdio.h>
#include <string.h>

/*
 * Stable AST dump.
 *
 * The exact text is part of the test suite: tests compare whole dumps, so
 * changing the format is a visible change, not an accident. Rules:
 *
 *   - one node per line, two spaces of indentation per level;
 *   - a closing paren is appended to the last child's line;
 *   - strings are double-quoted with C-style escapes;
 *   - `u` / `q` after a word part is its quoted flag (unquoted / quoted).
 */

static void wl_dump_indent(struct wl_buf *out, int depth) {
    for (int index = 0; index < depth; index++) {
        (void)wl_buf_append_str(out, "  ");
    }
}

static void wl_dump_escaped(struct wl_buf *out, const char *data, size_t len) {
    char escape[8];

    (void)wl_buf_append_char(out, '"');
    for (size_t index = 0; index < len; index++) {
        unsigned char byte = (unsigned char)data[index];

        switch (byte) {
        case '"':
            (void)wl_buf_append_str(out, "\\\"");
            break;
        case '\\':
            (void)wl_buf_append_str(out, "\\\\");
            break;
        case '\n':
            (void)wl_buf_append_str(out, "\\n");
            break;
        case '\t':
            (void)wl_buf_append_str(out, "\\t");
            break;
        case '\r':
            (void)wl_buf_append_str(out, "\\r");
            break;
        default:
            if (byte >= 0x20 && byte <= 0x7e) {
                (void)wl_buf_append_char(out, (char)byte);
            } else {
                (void)snprintf(escape, sizeof escape, "\\x%02x", byte);
                (void)wl_buf_append_str(out, escape);
            }
            break;
        }
    }
    (void)wl_buf_append_char(out, '"');
}

static const char *wl_dump_param_op(enum wl_param_op op, bool colon,
                                    char *buffer, size_t capacity) {
    const char *text;

    if (op == WL_PARAM_LENGTH) {
        return "length";
    }
    switch (op) {
    case WL_PARAM_DEFAULT:
        text = "-";
        break;
    case WL_PARAM_ASSIGN:
        text = "=";
        break;
    case WL_PARAM_ERROR:
        text = "?";
        break;
    case WL_PARAM_ALTERNATE:
        text = "+";
        break;
    case WL_PARAM_TRIM_PREFIX:
        text = "#";
        break;
    case WL_PARAM_TRIM_PREFIX_LONG:
        text = "##";
        break;
    case WL_PARAM_TRIM_SUFFIX:
        text = "%";
        break;
    case WL_PARAM_TRIM_SUFFIX_LONG:
        text = "%%";
        break;
    case WL_PARAM_NONE:
    default:
        return NULL;
    }
    (void)snprintf(buffer, capacity, "%s%s", colon ? ":" : "", text);
    return buffer;
}

static const char *wl_dump_redir_op(enum wl_redir_op op) {
    switch (op) {
    case WL_REDIR_IN:
        return "<";
    case WL_REDIR_OUT:
        return ">";
    case WL_REDIR_APPEND:
        return ">>";
    case WL_REDIR_READWRITE:
        return "<>";
    case WL_REDIR_CLOBBER:
        return ">|";
    case WL_REDIR_DUP_IN:
        return "<&";
    case WL_REDIR_DUP_OUT:
        return ">&";
    case WL_REDIR_HEREDOC:
        return "<<";
    case WL_REDIR_HEREDOC_TABS:
        return "<<-";
    case WL_REDIR_HERESTRING:
        return "<<<";
    default:
        return "?";
    }
}

static void wl_dump_node(const struct wl_node *node, struct wl_buf *out,
                         int depth);

static void wl_dump_word(const struct wl_word *word, struct wl_buf *out,
                         int depth);

static void wl_dump_word_part(const struct wl_word_part *part,
                              struct wl_buf *out, int depth) {
    char op_buffer[8];
    char number[32];

    switch (part->kind) {
    case WL_WP_LITERAL:
        (void)wl_buf_append_str(out, "(lit ");
        (void)wl_buf_append_str(out, part->quoted ? "q " : "u ");
        wl_dump_escaped(out, part->u.literal.data, part->u.literal.len);
        (void)wl_buf_append_char(out, ')');
        return;

    case WL_WP_TILDE:
        (void)wl_buf_append_str(out, "(tilde ");
        (void)wl_buf_append_str(out, part->quoted ? "q " : "u ");
        wl_dump_escaped(out, part->u.tilde.data, part->u.tilde.len);
        (void)wl_buf_append_char(out, ')');
        return;

    case WL_WP_ARITH:
        (void)wl_buf_append_str(out, "(arith ");
        (void)wl_buf_append_str(out, part->quoted ? "q " : "u ");
        wl_dump_escaped(out, part->u.arith.source.data,
                        part->u.arith.source.len);
        (void)wl_buf_append_char(out, ')');
        return;

    case WL_WP_CMDSUB:
        (void)wl_buf_append_str(out, "(cmdsub ");
        (void)wl_buf_append_str(out, part->quoted ? "q" : "u");
        if (part->u.cmdsub.program != NULL) {
            (void)wl_buf_append_char(out, '\n');
            wl_dump_indent(out, depth + 1);
            wl_dump_node(part->u.cmdsub.program, out, depth + 1);
            (void)wl_buf_append_char(out, ')');
        } else {
            (void)wl_buf_append_str(out, " source=");
            wl_dump_escaped(out, part->u.cmdsub.source.data,
                            part->u.cmdsub.source.len);
            (void)wl_buf_append_char(out, ')');
        }
        return;

    case WL_WP_PARAM: {
        const char *op_text =
            wl_dump_param_op(part->u.param.op, part->u.param.colon, op_buffer,
                             sizeof op_buffer);

        (void)wl_buf_append_str(out, "(param ");
        (void)wl_buf_append_str(out, part->quoted ? "q" : "u");
        if (part->u.param.name.len > 0) {
            (void)wl_buf_append_str(out, " name=");
            (void)wl_buf_append(out, part->u.param.name.data,
                                part->u.param.name.len);
        } else if (part->u.param.index >= 0) {
            (void)snprintf(number, sizeof number, " index=%d",
                           part->u.param.index);
            (void)wl_buf_append_str(out, number);
        } else {
            (void)snprintf(number, sizeof number, " special=%c",
                           part->u.param.special);
            (void)wl_buf_append_str(out, number);
        }
        if (op_text != NULL) {
            (void)wl_buf_append_str(out, " op=");
            (void)wl_buf_append_str(out, op_text);
        }
        if (part->u.param.word != NULL) {
            (void)wl_buf_append_char(out, ' ');
            wl_dump_word(part->u.param.word, out, depth);
        }
        (void)wl_buf_append_char(out, ')');
        return;
    }
    }
}

static void wl_dump_word(const struct wl_word *word, struct wl_buf *out,
                         int depth) {
    (void)wl_buf_append_str(out, "(word");
    if (word != NULL) {
        for (size_t index = 0; index < word->count; index++) {
            (void)wl_buf_append_char(out, ' ');
            wl_dump_word_part(&word->parts[index], out, depth);
        }
    }
    (void)wl_buf_append_char(out, ')');
}

static void wl_dump_redir(const struct wl_redir *redir, struct wl_buf *out) {
    char number[32];

    (void)wl_buf_append_str(out, "(redir ");
    if (redir->fd >= 0) {
        (void)snprintf(number, sizeof number, "%d", redir->fd);
        (void)wl_buf_append_str(out, number);
    }
    (void)wl_buf_append_str(out, wl_dump_redir_op(redir->op));
    (void)wl_buf_append_char(out, ' ');
    wl_dump_word(redir->target, out, 0);
    if (redir->heredoc != NULL) {
        (void)wl_buf_append_str(out, " body=");
        wl_dump_escaped(out, redir->heredoc->body.data,
                        redir->heredoc->body.len);
        (void)wl_buf_append_str(out,
                                redir->heredoc->expand ? " expand" : " literal");
        if (redir->heredoc->strip_tabs) {
            (void)wl_buf_append_str(out, " strip-tabs");
        }
    }
    (void)wl_buf_append_char(out, ')');
}

static void wl_dump_child(const struct wl_node *node, struct wl_buf *out,
                          int depth);

static void wl_dump_redirs(const struct wl_node *node, struct wl_buf *out,
                           int depth) {
    for (size_t index = 0; index < node->redir_count; index++) {
        (void)wl_buf_append_char(out, '\n');
        wl_dump_indent(out, depth);
        wl_dump_redir(node->redirs[index], out);
    }
}

static void wl_dump_child(const struct wl_node *node, struct wl_buf *out,
                          int depth) {
    (void)wl_buf_append_char(out, '\n');
    wl_dump_indent(out, depth);
    wl_dump_node(node, out, depth);
}

static void wl_dump_node(const struct wl_node *node, struct wl_buf *out,
                         int depth) {
    switch (node->kind) {
    case WL_NODE_LIST:
        (void)wl_buf_append_str(out, "(list");
        for (size_t index = 0; index < node->u.list.count; index++) {
            if (node->u.list.async[index]) {
                (void)wl_buf_append_char(out, '\n');
                wl_dump_indent(out, depth + 1);
                (void)wl_buf_append_str(out, "(async");
                wl_dump_child(node->u.list.statements[index], out, depth + 2);
                (void)wl_buf_append_char(out, ')');
            } else {
                wl_dump_child(node->u.list.statements[index], out, depth + 1);
            }
        }
        (void)wl_buf_append_char(out, ')');
        return;

    case WL_NODE_ANDOR:
        (void)wl_buf_append_str(out, "(andor");
        wl_dump_child(node->u.andor.first, out, depth + 1);
        for (size_t index = 0; index < node->u.andor.count; index++) {
            (void)wl_buf_append_char(out, '\n');
            wl_dump_indent(out, depth + 1);
            (void)wl_buf_append_str(out,
                                    node->u.andor.links[index].op == WL_ANDOR_AND
                                        ? "(and"
                                        : "(or");
            wl_dump_child(node->u.andor.links[index].node, out, depth + 2);
            (void)wl_buf_append_char(out, ')');
        }
        (void)wl_buf_append_char(out, ')');
        return;

    case WL_NODE_PIPELINE:
        (void)wl_buf_append_str(out, "(pipeline");
        if (node->u.pipeline.bang) {
            (void)wl_buf_append_str(out, " bang");
        }
        for (size_t index = 0; index < node->u.pipeline.count; index++) {
            wl_dump_child(node->u.pipeline.commands[index], out, depth + 1);
        }
        (void)wl_buf_append_char(out, ')');
        return;

    case WL_NODE_SIMPLE:
        (void)wl_buf_append_str(out, "(simple");
        for (size_t index = 0; index < node->u.simple.assign_count; index++) {
            (void)wl_buf_append_char(out, '\n');
            wl_dump_indent(out, depth + 1);
            (void)wl_buf_append_str(out, "(assign ");
            (void)wl_buf_append(out, node->u.simple.assign_names[index].data,
                                node->u.simple.assign_names[index].len);
            (void)wl_buf_append_char(out, ' ');
            wl_dump_word(node->u.simple.assign_values[index], out, depth + 1);
            (void)wl_buf_append_char(out, ')');
        }
        for (size_t index = 0; index < node->u.simple.word_count; index++) {
            (void)wl_buf_append_char(out, '\n');
            wl_dump_indent(out, depth + 1);
            wl_dump_word(node->u.simple.words[index], out, depth + 1);
        }
        wl_dump_redirs(node, out, depth + 1);
        (void)wl_buf_append_char(out, ')');
        return;

    case WL_NODE_SUBSHELL:
    case WL_NODE_GROUP:
        (void)wl_buf_append_str(out, node->kind == WL_NODE_SUBSHELL
                                         ? "(subshell"
                                         : "(group");
        wl_dump_child(node->u.group.body, out, depth + 1);
        wl_dump_redirs(node, out, depth + 1);
        (void)wl_buf_append_char(out, ')');
        return;

    case WL_NODE_FOR:
        (void)wl_buf_append_str(out, "(for name=");
        (void)wl_buf_append(out, node->u.forloop.name.data,
                            node->u.forloop.name.len);
        if (node->u.forloop.words_given) {
            (void)wl_buf_append_char(out, '\n');
            wl_dump_indent(out, depth + 1);
            (void)wl_buf_append_str(out, "(in");
            for (size_t index = 0; index < node->u.forloop.word_count; index++) {
                (void)wl_buf_append_char(out, '\n');
                wl_dump_indent(out, depth + 2);
                wl_dump_word(node->u.forloop.words[index], out, depth + 2);
            }
            (void)wl_buf_append_char(out, ')');
        }
        wl_dump_child(node->u.forloop.body, out, depth + 1);
        wl_dump_redirs(node, out, depth + 1);
        (void)wl_buf_append_char(out, ')');
        return;

    case WL_NODE_WHILE:
        (void)wl_buf_append_str(out, node->u.whileloop.until ? "(until"
                                                             : "(while");
        wl_dump_child(node->u.whileloop.condition, out, depth + 1);
        wl_dump_child(node->u.whileloop.body, out, depth + 1);
        wl_dump_redirs(node, out, depth + 1);
        (void)wl_buf_append_char(out, ')');
        return;

    case WL_NODE_IF:
        (void)wl_buf_append_str(out, "(if");
        for (size_t index = 0; index < node->u.ifclause.arm_count; index++) {
            (void)wl_buf_append_char(out, '\n');
            wl_dump_indent(out, depth + 1);
            (void)wl_buf_append_str(out, "(arm");
            wl_dump_child(node->u.ifclause.arms[index].condition, out,
                          depth + 2);
            wl_dump_child(node->u.ifclause.arms[index].body, out, depth + 2);
            (void)wl_buf_append_char(out, ')');
        }
        if (node->u.ifclause.else_body != NULL) {
            (void)wl_buf_append_char(out, '\n');
            wl_dump_indent(out, depth + 1);
            (void)wl_buf_append_str(out, "(else");
            wl_dump_child(node->u.ifclause.else_body, out, depth + 2);
            (void)wl_buf_append_char(out, ')');
        }
        wl_dump_redirs(node, out, depth + 1);
        (void)wl_buf_append_char(out, ')');
        return;

    case WL_NODE_CASE:
        (void)wl_buf_append_str(out, "(case");
        (void)wl_buf_append_char(out, '\n');
        wl_dump_indent(out, depth + 1);
        wl_dump_word(node->u.caseclause.word, out, depth + 1);
        for (size_t index = 0; index < node->u.caseclause.item_count; index++) {
            const struct wl_case_item *item = &node->u.caseclause.items[index];

            (void)wl_buf_append_char(out, '\n');
            wl_dump_indent(out, depth + 1);
            (void)wl_buf_append_str(out, "(item");
            for (size_t pattern = 0; pattern < item->pattern_count; pattern++) {
                (void)wl_buf_append_char(out, '\n');
                wl_dump_indent(out, depth + 2);
                wl_dump_word(item->patterns[pattern], out, depth + 2);
            }
            wl_dump_child(item->body, out, depth + 2);
            (void)wl_buf_append_char(out, ')');
        }
        wl_dump_redirs(node, out, depth + 1);
        (void)wl_buf_append_char(out, ')');
        return;

    case WL_NODE_FUNC:
        (void)wl_buf_append_str(out, "(func name=");
        (void)wl_buf_append(out, node->u.func.name.data, node->u.func.name.len);
        wl_dump_child(node->u.func.body, out, depth + 1);
        wl_dump_redirs(node, out, depth + 1);
        (void)wl_buf_append_char(out, ')');
        return;
    }
}

void wl_ast_dump(const struct wl_node *node, struct wl_buf *out) {
    if (node == NULL) {
        (void)wl_buf_append_str(out, "(nil)");
        return;
    }
    wl_dump_node(node, out, 0);
}
