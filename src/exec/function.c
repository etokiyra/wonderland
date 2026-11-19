#include "internal.h"

#include "ast/arena.h"
#include "ast/ast.h"
#include "core/diag.h"
#include "core/shell.h"
#include "parse/parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * User-defined functions.
 *
 * The body must outlive the parse unit that produced it, but the per-line
 * arena is reset after every command. A definition therefore copies its source
 * text, re-parses it into an arena owned by the function, and keeps the body
 * node. Redefining a function replaces it; the shell frees the table on exit.
 */

struct wl_function {
    char *name;
    struct wl_arena arena;
    struct wl_node *body;
    struct wl_function *next;
};

static struct wl_node *wl_first_function(struct wl_node *program) {
    struct wl_node *node;

    if (program == NULL || program->kind != WL_NODE_LIST ||
        program->u.list.count == 0) {
        return NULL;
    }
    node = program->u.list.statements[0];
    if (node->kind != WL_NODE_ANDOR) {
        return NULL;
    }
    node = node->u.andor.first;
    if (node->kind != WL_NODE_PIPELINE || node->u.pipeline.count == 0) {
        return NULL;
    }
    node = node->u.pipeline.commands[0];
    return (node->kind == WL_NODE_FUNC) ? node : NULL;
}

static struct wl_function *wl_find_function(struct wl_shell *shell,
                                            const char *name, size_t length) {
    for (struct wl_function *function = shell->functions; function != NULL;
         function = function->next) {
        if (strlen(function->name) == length &&
            memcmp(function->name, name, length) == 0) {
            return function;
        }
    }
    return NULL;
}

static void wl_free_function(struct wl_function *function) {
    free(function->name);
    wl_arena_reset(&function->arena);
    free(function);
}

bool wl_exec_function_define(struct wl_shell *shell,
                             const struct wl_node *definition) {
    const struct wl_str *name = &definition->u.func.name;
    struct wl_function *existing;
    struct wl_function *function;
    struct wl_arena arena;
    struct wl_diag diag;
    struct wl_node *program;
    struct wl_node *parsed;
    char *text;
    size_t length;
    size_t start = definition->span.start;
    size_t end = definition->span.end;

    if (shell->source == NULL || end < start || end > shell->source_length) {
        fprintf(stderr,
                "wonderland: cannot store function '%s': source is not "
                "available\n",
                name->data);
        return false;
    }

    length = end - start;
    text = malloc(length + 1);
    if (text == NULL) {
        fprintf(stderr, "wonderland: out of memory\n");
        return false;
    }
    memcpy(text, shell->source + start, length);
    text[length] = '\0';

    wl_arena_init(&arena);
    wl_diag_clear(&diag);
    program = wl_parse_source(text, length, "<function>", &arena, &diag);
    free(text);
    if (program == NULL) {
        if (diag.set) {
            wl_diag_print(&diag, shell->source + start, length,
                          "<function>", stderr);
        }
        wl_arena_reset(&arena);
        return false;
    }
    parsed = wl_first_function(program);
    if (parsed == NULL) {
        fprintf(stderr, "wonderland: internal error: lost function body\n");
        wl_arena_reset(&arena);
        return false;
    }

    existing = wl_find_function(shell, name->data, name->len);
    if (existing != NULL) {
        existing->body = parsed->u.func.body;
        wl_arena_reset(&existing->arena);
        existing->arena = arena;
        return true;
    }

    function = calloc(1, sizeof *function);
    if (function == NULL) {
        wl_arena_reset(&arena);
        fprintf(stderr, "wonderland: out of memory\n");
        return false;
    }
    function->name = malloc(name->len + 1);
    if (function->name == NULL) {
        free(function);
        wl_arena_reset(&arena);
        fprintf(stderr, "wonderland: out of memory\n");
        return false;
    }
    memcpy(function->name, name->data, name->len);
    function->name[name->len] = '\0';
    function->arena = arena;
    function->body = parsed->u.func.body;
    function->next = shell->functions;
    shell->functions = function;
    return true;
}

bool wl_exec_function_exists(struct wl_shell *shell, const char *name,
                             size_t length) {
    return wl_find_function(shell, name, length) != NULL;
}

bool wl_exec_function_call(struct wl_shell *shell, const char *name,
                           int argc, char **argv) {
    struct wl_function *function =
        wl_find_function(shell, name, strlen(name));
    int saved_loop_depth;
    int saved_break;
    int saved_continue;
    bool saved_returning;
    int saved_return_status;
    bool ok;

    if (function == NULL) {
        return false;
    }

    if (!wl_shell_push_frame(shell, function->name,
                             (char *const *)(argv + 1),
                             (argc > 1) ? (size_t)(argc - 1) : 0)) {
        fprintf(stderr, "wonderland: out of memory\n");
        wl_shell_set_status(shell, 1);
        return true;
    }

    saved_loop_depth = shell->loop_depth;
    saved_break = shell->break_count;
    saved_continue = shell->continue_count;
    saved_returning = shell->returning;
    saved_return_status = shell->return_status;

    shell->loop_depth = 0;
    shell->break_count = 0;
    shell->continue_count = 0;
    shell->returning = false;
    shell->return_status = 0;
    shell->function_depth++;

    ok = wl_exec_node(shell, function->body);

    shell->function_depth--;
    if (shell->returning) {
        wl_shell_set_status(shell, shell->return_status);
    }
    shell->returning = saved_returning;
    shell->return_status = saved_return_status;
    shell->loop_depth = saved_loop_depth;
    shell->break_count = saved_break;
    shell->continue_count = saved_continue;

    wl_shell_pop_frame(shell);
    return ok;
}

void wl_exec_function_free(struct wl_shell *shell) {
    struct wl_function *function = shell->functions;

    while (function != NULL) {
        struct wl_function *next = function->next;

        wl_free_function(function);
        function = next;
    }
    shell->functions = NULL;
}
