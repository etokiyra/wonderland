#include "exec/exec.h"

#include "ast/arena.h"
#include "ast/ast.h"
#include "builtins/builtins.h"
#include "core/buf.h"
#include "core/diag.h"
#include "core/limits.h"
#include "core/shell.h"
#include "core/var.h"
#include "expand/expand.h"
#include "exec/path.h"
#include "jobs/signal.h"
#include "lex/lex.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Execution, M3a.
 *
 * The pipeline is the unit that owns processes: every stage gets its own
 * child, the first stage's process group owns the terminal, and the status is
 * the last stage's. Redirections are expanded in the parent (so errors are
 * reported once, in order) and applied in the child, or around a builtin with
 * file-descriptor save/restore.
 *
 * Compound commands are parsed but still refused: M3b.
 */

struct wl_prepared_redir {
    int fd;
    enum wl_redir_op op;
    char *target; /* owned, NULL for here-documents and here-strings */
    int body_fd;  /* here-document/here-string source, or -1 */
};

struct wl_prepared {
    struct wl_prepared_redir *items;
    size_t count;
    size_t capacity;
};

struct wl_saved_var {
    char *name;
    char *value;
    bool existed;
    bool exported;
};

static bool wl_exec_node(struct wl_shell *shell, const struct wl_node *node);

static void wl_prepared_init(struct wl_prepared *prepared) {
    prepared->items = NULL;
    prepared->count = 0;
    prepared->capacity = 0;
}

static void wl_prepared_free(struct wl_prepared *prepared) {
    for (size_t index = 0; index < prepared->count; index++) {
        if (prepared->items[index].body_fd >= 0) {
            (void)close(prepared->items[index].body_fd);
        }
        free(prepared->items[index].target);
    }
    free(prepared->items);
    prepared->items = NULL;
    prepared->count = 0;
    prepared->capacity = 0;
}

static bool wl_prepared_push(struct wl_prepared *prepared,
                             const struct wl_prepared_redir *item) {
    if (prepared->count == prepared->capacity) {
        size_t capacity = (prepared->capacity > 0) ? prepared->capacity * 2 : 4;
        struct wl_prepared_redir *grown =
            realloc(prepared->items, capacity * sizeof *grown);

        if (grown == NULL) {
            return false;
        }
        prepared->items = grown;
        prepared->capacity = capacity;
    }
    prepared->items[prepared->count++] = *item;
    return true;
}

static void wl_expand_ctx_init(struct wl_expand_ctx *ctx,
                               struct wl_shell *shell);

static void wl_report_expand_error(const struct wl_expand_ctx *ctx) {
    fprintf(stderr, "wonderland: %s\n",
            (ctx->error[0] != '\0') ? ctx->error : "expansion failed");
}

static int wl_make_body_fd(const char *data, size_t length) {
    FILE *file = tmpfile();
    int fd;

    if (file == NULL) {
        return -1;
    }
    if (length > 0 && fwrite(data, 1, length, file) != length) {
        (void)fclose(file);
        return -1;
    }
    (void)fflush(file);
    rewind(file);
    fd = dup(fileno(file));
    (void)fclose(file);
    if (fd >= 0) {
        (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    return fd;
}

static bool wl_expand_heredoc(struct wl_expand_ctx *ctx,
                              const struct wl_heredoc *heredoc,
                              struct wl_buf *out) {
    struct wl_arena arena;
    struct wl_diag diag;
    struct wl_lexer lexer;
    struct wl_word *word = NULL;
    bool ok;

    if (!heredoc->expand) {
        return wl_buf_append(out, heredoc->body.data, heredoc->body.len);
    }

    wl_arena_init(&arena);
    wl_diag_clear(&diag);
    wl_lex_init(&lexer, heredoc->body.data, heredoc->body.len, &arena, &diag);

    ok = wl_lex_word_heredoc(&lexer, &word);
    if (ok) {
        ok = wl_expand_single(ctx, word, out);
    }
    if (!ok) {
        if (diag.set) {
            wl_diag_print(&diag, heredoc->body.data, heredoc->body.len,
                          "here-document", stderr);
        } else {
            wl_report_expand_error(ctx);
        }
    }
    wl_arena_reset(&arena);
    return ok;
}

static bool wl_prepare_redirs(struct wl_expand_ctx *ctx,
                              const struct wl_node *node,
                              struct wl_prepared *prepared) {
    for (size_t index = 0; index < node->redir_count; index++) {
        const struct wl_redir *redir = node->redirs[index];
        struct wl_prepared_redir item;

        item.fd = redir->fd;
        item.op = redir->op;
        item.target = NULL;
        item.body_fd = -1;

        if (redir->op == WL_REDIR_HEREDOC ||
            redir->op == WL_REDIR_HEREDOC_TABS) {
            struct wl_buf body;

            wl_buf_init(&body);
            if (!wl_expand_heredoc(ctx, redir->heredoc, &body)) {
                wl_buf_free(&body);
                wl_prepared_free(prepared);
                return false;
            }
            item.body_fd = wl_make_body_fd(body.data, body.len);
            wl_buf_free(&body);
            if (item.body_fd < 0) {
                fprintf(stderr, "wonderland: here-document: %s\n",
                        strerror(errno));
                wl_prepared_free(prepared);
                return false;
            }
        } else if (redir->op == WL_REDIR_HERESTRING) {
            struct wl_buf body;

            wl_buf_init(&body);
            if (!wl_expand_single(ctx, redir->target, &body)) {
                wl_report_expand_error(ctx);
                wl_buf_free(&body);
                wl_prepared_free(prepared);
                return false;
            }
            (void)wl_buf_append_char(&body, '\n');
            item.body_fd = wl_make_body_fd(body.data, body.len);
            wl_buf_free(&body);
            if (item.body_fd < 0) {
                fprintf(stderr, "wonderland: here-string: %s\n",
                        strerror(errno));
                wl_prepared_free(prepared);
                return false;
            }
        } else {
            bool ambiguous = false;

            if (!wl_expand_target(ctx, redir->target, &item.target,
                                  &ambiguous)) {
                wl_report_expand_error(ctx);
                wl_prepared_free(prepared);
                return false;
            }
            if (ambiguous) {
                fprintf(stderr, "wonderland: ambiguous redirect\n");
                wl_prepared_free(prepared);
                return false;
            }
        }

        if (!wl_prepared_push(prepared, &item)) {
            fprintf(stderr, "wonderland: out of memory\n");
            free(item.target);
            if (item.body_fd >= 0) {
                (void)close(item.body_fd);
            }
            wl_prepared_free(prepared);
            return false;
        }
    }
    return true;
}

static bool wl_apply_one_redir(const struct wl_prepared_redir *redir) {
    int opened = -1;

    switch (redir->op) {
    case WL_REDIR_IN:
        opened = open(redir->target, O_RDONLY);
        break;
    case WL_REDIR_OUT:
    case WL_REDIR_CLOBBER:
        opened = open(redir->target, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        break;
    case WL_REDIR_APPEND:
        opened = open(redir->target, O_WRONLY | O_CREAT | O_APPEND, 0666);
        break;
    case WL_REDIR_READWRITE:
        opened = open(redir->target, O_RDWR | O_CREAT, 0666);
        break;
    case WL_REDIR_HEREDOC:
    case WL_REDIR_HEREDOC_TABS:
    case WL_REDIR_HERESTRING:
        opened = redir->body_fd;
        break;
    case WL_REDIR_DUP_IN:
    case WL_REDIR_DUP_OUT: {
        char *end = NULL;
        long descriptor;

        if (strcmp(redir->target, "-") == 0) {
            (void)close(redir->fd);
            return true;
        }
        errno = 0;
        descriptor = strtol(redir->target, &end, 10);
        if (errno != 0 || end == redir->target || *end != '\0' ||
            descriptor < 0 || descriptor > INT_MAX) {
            fprintf(stderr, "wonderland: %s: bad file descriptor\n",
                    redir->target);
            return false;
        }
        if (dup2((int)descriptor, redir->fd) < 0) {
            fprintf(stderr, "wonderland: %s: %s\n", redir->target,
                    strerror(errno));
            return false;
        }
        return true;
    }
    }

    if (opened < 0) {
        fprintf(stderr, "wonderland: %s: %s\n",
                (redir->target != NULL) ? redir->target : "redirection",
                strerror(errno));
        return false;
    }
    if (dup2(opened, redir->fd) < 0) {
        fprintf(stderr, "wonderland: %s: %s\n",
                (redir->target != NULL) ? redir->target : "redirection",
                strerror(errno));
        if (opened != redir->fd) {
            (void)close(opened);
        }
        return false;
    }
    if (opened != redir->fd) {
        (void)close(opened);
    }
    return true;
}

static bool wl_apply_redirs(const struct wl_prepared *prepared) {
    for (size_t index = 0; index < prepared->count; index++) {
        if (!wl_apply_one_redir(&prepared->items[index])) {
            return false;
        }
    }
    return true;
}

struct wl_fd_guard {
    int *fds;
    int *saved;
    size_t count;
};

static bool wl_guard_begin(struct wl_shell *shell,
                           const struct wl_prepared *prepared,
                           struct wl_fd_guard *guard) {
    size_t count = prepared->count;

    guard->fds = NULL;
    guard->saved = NULL;
    guard->count = count;
    if (count == 0) {
        return true;
    }
    guard->fds = calloc(count, sizeof *guard->fds);
    guard->saved = calloc(count, sizeof *guard->saved);
    if (guard->fds == NULL || guard->saved == NULL) {
        free(guard->fds);
        free(guard->saved);
        guard->fds = NULL;
        guard->saved = NULL;
        guard->count = 0;
        fprintf(stderr, "wonderland: out of memory\n");
        wl_shell_set_status(shell, 1);
        return false;
    }
    for (size_t index = 0; index < count; index++) {
        guard->fds[index] = prepared->items[index].fd;
        guard->saved[index] = fcntl(guard->fds[index], F_DUPFD, 10);
    }
    if (!wl_apply_redirs(prepared)) {
        wl_shell_set_status(shell, 1);
        return false;
    }
    return true;
}

static void wl_guard_end(struct wl_fd_guard *guard) {
    for (size_t index = 0; index < guard->count; index++) {
        if (guard->saved[index] >= 0) {
            (void)dup2(guard->saved[index], guard->fds[index]);
            (void)close(guard->saved[index]);
        } else {
            (void)close(guard->fds[index]);
        }
    }
    free(guard->fds);
    free(guard->saved);
    guard->fds = NULL;
    guard->saved = NULL;
    guard->count = 0;
}

static bool wl_run_builtin_redirected(struct wl_shell *shell,
                                      const struct wl_prepared *prepared,
                                      int argc, char **argv) {
    struct wl_fd_guard guard;
    bool applied = wl_guard_begin(shell, prepared, &guard);

    if (applied) {
        if (!wl_builtin_run(shell, argc, argv)) {
            fprintf(stderr, "wonderland: %s: not a builtin\n", argv[0]);
            wl_shell_set_status(shell, 127);
            applied = false;
        }
    }
    wl_guard_end(&guard);
    return applied;
}

static bool wl_apply_redirect_only(struct wl_shell *shell,
                                   const struct wl_prepared *prepared) {
    struct wl_fd_guard guard;
    bool applied = wl_guard_begin(shell, prepared, &guard);

    wl_guard_end(&guard);
    if (applied) {
        wl_shell_set_status(shell, 0);
    }
    return applied;
}

static char *wl_duplicate(const char *text) {
    size_t length;
    char *copy;

    if (text == NULL) {
        return NULL;
    }
    length = strlen(text);
    copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, text, length + 1);
    }
    return copy;
}

static bool wl_apply_assignments(struct wl_expand_ctx *ctx,
                                 const struct wl_node *node) {
    for (size_t index = 0; index < node->u.simple.assign_count; index++) {
        struct wl_buf value;

        wl_buf_init(&value);
        if (!wl_expand_single(ctx, node->u.simple.assign_values[index],
                              &value)) {
            wl_report_expand_error(ctx);
            wl_buf_free(&value);
            return false;
        }
        if (!wl_var_set(&ctx->shell->vars,
                        node->u.simple.assign_names[index].data,
                        node->u.simple.assign_names[index].len,
                        (value.data != NULL) ? value.data : "", value.len,
                        false)) {
            fprintf(stderr, "wonderland: out of memory\n");
            wl_buf_free(&value);
            return false;
        }
        wl_buf_free(&value);
    }
    return true;
}

static void wl_restore_assignments(struct wl_shell *shell,
                                   struct wl_saved_var *saved, size_t count) {
    for (size_t index = 0; index < count; index++) {
        if (saved[index].name == NULL) {
            continue;
        }
        if (saved[index].existed) {
            (void)wl_var_set(&shell->vars, saved[index].name,
                             strlen(saved[index].name), saved[index].value,
                             strlen(saved[index].value), saved[index].exported);
        } else {
            (void)wl_var_unset(&shell->vars, saved[index].name,
                               strlen(saved[index].name));
        }
        free(saved[index].name);
        free(saved[index].value);
    }
    free(saved);
}

/*
 * Applies prefix assignments for the duration of a builtin. They are part of
 * the command environment, not a permanent shell assignment for regular
 * builtins (docs/DECISIONS.md D-0062).
 */
static bool wl_save_and_apply_assignments(struct wl_expand_ctx *ctx,
                                          const struct wl_node *node,
                                          struct wl_saved_var **out,
                                          size_t *out_count) {
    size_t count = node->u.simple.assign_count;
    struct wl_saved_var *saved;

    *out = NULL;
    *out_count = 0;
    if (count == 0) {
        return true;
    }
    saved = calloc(count, sizeof *saved);
    if (saved == NULL) {
        fprintf(stderr, "wonderland: out of memory\n");
        return false;
    }

    for (size_t index = 0; index < count; index++) {
        const struct wl_str *name = &node->u.simple.assign_names[index];
        const char *old = wl_var_get(&ctx->shell->vars, name->data, name->len);

        saved[index].name = malloc(name->len + 1);
        if (saved[index].name == NULL) {
            wl_restore_assignments(ctx->shell, saved, index);
            return false;
        }
        memcpy(saved[index].name, name->data, name->len);
        saved[index].name[name->len] = '\0';
        saved[index].existed = old != NULL;
        saved[index].value = wl_duplicate(old);
        if (old != NULL && saved[index].value == NULL) {
            wl_restore_assignments(ctx->shell, saved, index + 1);
            return false;
        }
        saved[index].exported =
            wl_var_is_exported(&ctx->shell->vars, name->data, name->len);
    }

    if (!wl_apply_assignments(ctx, node)) {
        wl_restore_assignments(ctx->shell, saved, count);
        return false;
    }

    *out = saved;
    *out_count = count;
    return true;
}

static char **wl_build_env(struct wl_expand_ctx *ctx,
                           const struct wl_node *node) {
    struct wl_shell *shell = ctx->shell;
    char **base = wl_vars_build_environ(&shell->vars);
    size_t base_count = 0;
    size_t extra = node->u.simple.assign_count;
    char **environment;

    if (base == NULL) {
        fprintf(stderr, "wonderland: out of memory\n");
        return NULL;
    }
    while (base[base_count] != NULL) {
        base_count++;
    }
    environment = calloc(base_count + extra + 1, sizeof *environment);
    if (environment == NULL) {
        wl_environ_free(base);
        fprintf(stderr, "wonderland: out of memory\n");
        return NULL;
    }
    for (size_t index = 0; index < base_count; index++) {
        environment[index] = base[index];
    }
    free(base);

    for (size_t index = 0; index < extra; index++) {
        const struct wl_str *name = &node->u.simple.assign_names[index];
        struct wl_buf value;
        size_t name_len = name->len;
        size_t value_len;
        char *entry;

        wl_buf_init(&value);
        if (!wl_expand_single(ctx, node->u.simple.assign_values[index],
                              &value)) {
            wl_report_expand_error(ctx);
            wl_buf_free(&value);
            wl_environ_free(environment);
            return NULL;
        }
        value_len = value.len;
        entry = malloc(name_len + value_len + 2);
        if (entry == NULL) {
            wl_buf_free(&value);
            fprintf(stderr, "wonderland: out of memory\n");
            wl_environ_free(environment);
            return NULL;
        }
        memcpy(entry, name->data, name_len);
        entry[name_len] = '=';
        if (value_len > 0) {
            memcpy(entry + name_len + 1, value.data, value_len);
        }
        entry[name_len + value_len + 1] = '\0';
        environment[base_count + index] = entry;
        wl_buf_free(&value);
    }
    environment[base_count + extra] = NULL;
    return environment;
}

static void wl_exec_child(struct wl_shell *shell, char *const argv[],
                          char **environment) {
    char path[WL_PATH_MAX];
    enum wl_path_status found = wl_path_find(argv[0], path, sizeof path);

    (void)shell;

    if (found == WL_PATH_NOT_FOUND) {
        fprintf(stderr, "wonderland: %s: command not found\n", argv[0]);
        _exit(127);
    }
    if (found == WL_PATH_NOT_EXECUTABLE) {
        fprintf(stderr, "wonderland: %s: Permission denied\n", argv[0]);
        _exit(126);
    }
    if (found == WL_PATH_ERROR) {
        fprintf(stderr, "wonderland: %s: %s\n", argv[0], strerror(errno));
        _exit(126);
    }

    execve(path, argv, environment);
    fprintf(stderr, "wonderland: %s: %s\n", argv[0], strerror(errno));
    _exit(errno == ENOENT ? 127 : 126);
}

static void wl_terminal_give(int tty_fd, pid_t pgid) {
    if (tty_fd < 0) {
        return;
    }
    while (tcsetpgrp(tty_fd, pgid) != 0 && errno == EINTR) {
        /* retry: a signal can interrupt the ioctl */
    }
}

static int wl_wait_status(pid_t pid) {
    int status = 0;
    pid_t reaped;

    do {
        reaped = waitpid(pid, &status, 0);
    } while (reaped < 0 && errno == EINTR);

    if (reaped < 0) {
        fprintf(stderr, "wonderland: wait: %s\n", strerror(errno));
        return 1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

static bool wl_run_external(struct wl_shell *shell, char *const argv[],
                            char **environment,
                            const struct wl_prepared *prepared) {
    pid_t child = fork();

    if (child < 0) {
        fprintf(stderr, "wonderland: fork: %s\n", strerror(errno));
        wl_shell_set_status(shell, 1);
        return false;
    }
    if (child == 0) {
        wl_signal_reset_for_child();
        if (shell->tty_fd >= 0) {
            (void)setpgid(0, 0);
        }
        if (!wl_apply_redirs(prepared)) {
            _exit(1);
        }
        wl_exec_child(shell, argv, environment);
    }

    if (shell->tty_fd >= 0) {
        (void)setpgid(child, child);
        wl_terminal_give(shell->tty_fd, child);
    }
    wl_shell_set_status(shell, wl_wait_status(child));
    wl_terminal_give(shell->tty_fd, shell->shell_pgid);
    return true;
}

static bool wl_capture_program(void *context, const struct wl_node *program,
                               struct wl_buf *output, int *status) {
    struct wl_shell *shell = context;
    int fds[2];
    pid_t child;
    int wait_status = 0;
    pid_t reaped;

    if (pipe(fds) != 0) {
        fprintf(stderr, "wonderland: pipe: %s\n", strerror(errno));
        return false;
    }
    fflush(NULL);
    child = fork();
    if (child < 0) {
        fprintf(stderr, "wonderland: fork: %s\n", strerror(errno));
        (void)close(fds[0]);
        (void)close(fds[1]);
        return false;
    }
    if (child == 0) {
        (void)close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) {
            _exit(1);
        }
        (void)close(fds[1]);
        (void)wl_exec_program(shell, program);
        _exit(shell->last_status & 0xff);
    }

    (void)close(fds[1]);
    for (;;) {
        char chunk[4096];
        ssize_t count = read(fds[0], chunk, sizeof chunk);

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (count == 0) {
            break;
        }
        if (!wl_buf_append(output, chunk, (size_t)count)) {
            fprintf(stderr, "wonderland: out of memory\n");
            break;
        }
    }
    (void)close(fds[0]);

    do {
        reaped = waitpid(child, &wait_status, 0);
    } while (reaped < 0 && errno == EINTR);

    if (reaped < 0) {
        *status = 1;
    } else if (WIFEXITED(wait_status)) {
        *status = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        *status = 128 + WTERMSIG(wait_status);
    } else {
        *status = 1;
    }
    return true;
}

static void wl_expand_ctx_init(struct wl_expand_ctx *ctx,
                               struct wl_shell *shell) {
    memset(ctx, 0, sizeof *ctx);
    ctx->shell = shell;
    ctx->capture = wl_capture_program;
    ctx->capture_context = shell;
}

static bool wl_exec_simple(struct wl_shell *shell, const struct wl_node *node) {
    struct wl_expand_ctx ctx;
    struct wl_argv argv;
    struct wl_prepared prepared;
    bool ok = true;

    wl_expand_ctx_init(&ctx, shell);
    wl_argv_init(&argv);
    wl_prepared_init(&prepared);

    if (node->u.simple.word_count == 0) {
        if (!wl_apply_assignments(&ctx, node)) {
            wl_shell_set_status(shell, 1);
            return false;
        }
        if (node->redir_count > 0) {
            if (!wl_prepare_redirs(&ctx, node, &prepared)) {
                wl_shell_set_status(shell, 1);
                return false;
            }
            ok = wl_apply_redirect_only(shell, &prepared);
        } else {
            wl_shell_set_status(shell, 0);
        }
        wl_prepared_free(&prepared);
        return ok;
    }

    if (!wl_expand_words(&ctx, node->u.simple.words,
                         node->u.simple.word_count, &argv)) {
        wl_report_expand_error(&ctx);
        wl_shell_set_status(shell, 1);
        wl_argv_free(&argv);
        return false;
    }

    if (argv.count == 0) {
        /* All words expanded away; redirections still happen. */
        if (node->redir_count > 0) {
            if (!wl_prepare_redirs(&ctx, node, &prepared)) {
                wl_shell_set_status(shell, 1);
                wl_argv_free(&argv);
                return false;
            }
            ok = wl_apply_redirect_only(shell, &prepared);
        } else {
            wl_shell_set_status(shell, 0);
        }
        wl_prepared_free(&prepared);
        wl_argv_free(&argv);
        return ok;
    }

    if (!wl_prepare_redirs(&ctx, node, &prepared)) {
        wl_shell_set_status(shell, 1);
        wl_argv_free(&argv);
        return false;
    }

    if (wl_builtin_is(argv.items[0])) {
        struct wl_saved_var *saved = NULL;
        size_t saved_count = 0;

        if (!wl_save_and_apply_assignments(&ctx, node, &saved,
                                           &saved_count)) {
            wl_shell_set_status(shell, 1);
            ok = false;
        } else {
            ok = wl_run_builtin_redirected(shell, &prepared, (int)argv.count,
                                           argv.items);
            wl_restore_assignments(shell, saved, saved_count);
        }
    } else {
        char **environment = wl_build_env(&ctx, node);

        if (environment == NULL) {
            wl_shell_set_status(shell, 1);
            ok = false;
        } else {
            ok = wl_run_external(shell, argv.items, environment, &prepared);
            wl_environ_free(environment);
        }
    }

    wl_prepared_free(&prepared);
    wl_argv_free(&argv);
    return ok;
}

struct wl_stage {
    struct wl_argv argv;
    struct wl_prepared redirs;
    char **environment;
    bool builtin;
};

static void wl_stage_free(struct wl_stage *stage) {
    wl_argv_free(&stage->argv);
    wl_prepared_free(&stage->redirs);
    wl_environ_free(stage->environment);
    stage->environment = NULL;
}

static bool wl_prepare_stage(struct wl_expand_ctx *ctx,
                             const struct wl_node *node,
                             struct wl_stage *stage) {
    wl_argv_init(&stage->argv);
    wl_prepared_init(&stage->redirs);
    stage->environment = NULL;
    stage->builtin = false;

    if (node->kind != WL_NODE_SIMPLE) {
        fprintf(stderr,
                "wonderland: a compound command inside a pipeline is not "
                "implemented yet (M3b)\n");
        return false;
    }
    if (!wl_expand_words(ctx, node->u.simple.words,
                         node->u.simple.word_count, &stage->argv)) {
        wl_report_expand_error(ctx);
        return false;
    }
    if (stage->argv.count == 0) {
        fprintf(stderr, "wonderland: empty command in a pipeline\n");
        return false;
    }
    stage->builtin = wl_builtin_is(stage->argv.items[0]);
    if (!stage->builtin) {
        stage->environment = wl_build_env(ctx, node);
        if (stage->environment == NULL) {
            return false;
        }
    }
    if (!wl_prepare_redirs(ctx, node, &stage->redirs)) {
        return false;
    }
    return true;
}

static bool wl_exec_pipeline(struct wl_shell *shell,
                             const struct wl_node *node) {
    size_t count = node->u.pipeline.count;
    struct wl_expand_ctx ctx;
    struct wl_stage *stages;
    pid_t *children;
    int status = 1;
    int previous_read = -1;
    pid_t first_child = -1;
    bool ok = true;
    size_t started = 0;

    if (count == 1) {
        ok = wl_exec_node(shell, node->u.pipeline.commands[0]);
    } else {
        wl_expand_ctx_init(&ctx, shell);
        stages = calloc(count, sizeof *stages);
        children = calloc(count, sizeof *children);
        if (stages == NULL || children == NULL) {
            fprintf(stderr, "wonderland: out of memory\n");
            free(stages);
            free(children);
            wl_shell_set_status(shell, 1);
            return false;
        }

        for (size_t index = 0; index < count; index++) {
            if (!wl_prepare_stage(&ctx, node->u.pipeline.commands[index],
                                  &stages[index])) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            wl_shell_set_status(shell, 1);
            for (size_t index = 0; index < count; index++) {
                wl_stage_free(&stages[index]);
            }
            free(stages);
            free(children);
            return false;
        }

        for (size_t index = 0; index < count; index++) {
            int pipe_fds[2] = {-1, -1};
            pid_t child;

            if (index + 1 < count && pipe(pipe_fds) != 0) {
                fprintf(stderr, "wonderland: pipe: %s\n", strerror(errno));
                ok = false;
                break;
            }
            child = fork();
            if (child < 0) {
                fprintf(stderr, "wonderland: fork: %s\n", strerror(errno));
                if (pipe_fds[0] >= 0) {
                    (void)close(pipe_fds[0]);
                    (void)close(pipe_fds[1]);
                }
                ok = false;
                break;
            }
            if (child == 0) {
                wl_signal_reset_for_child();
                if (shell->tty_fd >= 0) {
                    (void)setpgid(0, (first_child >= 0) ? first_child : 0);
                }
                if (previous_read >= 0) {
                    if (dup2(previous_read, STDIN_FILENO) < 0) {
                        _exit(1);
                    }
                    (void)close(previous_read);
                }
                if (index + 1 < count) {
                    if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
                        _exit(1);
                    }
                    (void)close(pipe_fds[1]);
                    (void)close(pipe_fds[0]);
                }
                if (!wl_apply_redirs(&stages[index].redirs)) {
                    _exit(1);
                }
                if (stages[index].builtin) {
                    (void)wl_builtin_run(shell, (int)stages[index].argv.count,
                                         stages[index].argv.items);
                    _exit(shell->last_status & 0xff);
                }
                wl_exec_child(shell, stages[index].argv.items,
                              stages[index].environment);
            }

            children[index] = child;
            started++;
            if (first_child < 0) {
                first_child = child;
                wl_terminal_give(shell->tty_fd, child);
            } else if (shell->tty_fd >= 0) {
                (void)setpgid(child, first_child);
            }
            if (previous_read >= 0) {
                (void)close(previous_read);
            }
            if (index + 1 < count) {
                (void)close(pipe_fds[1]);
                previous_read = pipe_fds[0];
            } else {
                previous_read = -1;
            }
        }

        if (previous_read >= 0) {
            (void)close(previous_read);
        }

        for (size_t index = 0; index < started; index++) {
            int child_status = wl_wait_status(children[index]);

            if (index + 1 == count) {
                status = child_status;
            }
        }
        wl_terminal_give(shell->tty_fd, shell->shell_pgid);
        wl_shell_set_status(shell, status);

        for (size_t index = 0; index < count; index++) {
            wl_stage_free(&stages[index]);
        }
        free(stages);
        free(children);
    }

    if (node->u.pipeline.bang) {
        wl_shell_set_status(shell, (shell->last_status == 0) ? 1 : 0);
    }
    return ok;
}

static bool wl_exec_node(struct wl_shell *shell, const struct wl_node *node);

static bool wl_exec_andor(struct wl_shell *shell, const struct wl_node *node) {
    if (!wl_exec_node(shell, node->u.andor.first)) {
        return false;
    }
    for (size_t index = 0; index < node->u.andor.count; index++) {
        const struct wl_andor_link *link = &node->u.andor.links[index];
        bool run = (link->op == WL_ANDOR_AND)
                       ? (shell->last_status == 0)
                       : (shell->last_status != 0);

        if (run && !wl_exec_node(shell, link->node)) {
            return false;
        }
    }
    return true;
}

static bool wl_exec_list(struct wl_shell *shell, const struct wl_node *node) {
    for (size_t index = 0; index < node->u.list.count; index++) {
        if (!shell->running) {
            break;
        }
        if (node->u.list.async[index]) {
            fprintf(stderr,
                    "wonderland: a background job is not implemented yet "
                    "(M4)\n");
            wl_shell_set_status(shell, 2);
            return false;
        }
        if (!wl_exec_node(shell, node->u.list.statements[index])) {
            return false;
        }
    }
    return true;
}

static bool wl_exec_node(struct wl_shell *shell, const struct wl_node *node) {
    switch (node->kind) {
    case WL_NODE_LIST:
        return wl_exec_list(shell, node);
    case WL_NODE_ANDOR:
        return wl_exec_andor(shell, node);
    case WL_NODE_PIPELINE:
        return wl_exec_pipeline(shell, node);
    case WL_NODE_SIMPLE:
        return wl_exec_simple(shell, node);
    default:
        fprintf(stderr,
                "wonderland: a compound command is not implemented yet "
                "(M3b)\n");
        wl_shell_set_status(shell, 2);
        return false;
    }
}

bool wl_exec_program(struct wl_shell *shell, const struct wl_node *program) {
    if (program == NULL) {
        return true;
    }
    return wl_exec_node(shell, program);
}
