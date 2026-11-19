#ifndef WL_AST_AST_H
#define WL_AST_AST_H

#include <stdbool.h>
#include <stddef.h>

#include "core/buf.h"

/*
 * Shell syntax tree.
 *
 * The AST is pure syntax: it records what was written, including which bytes
 * were quoted. It contains no runtime state, no expansion results, and no
 * decisions about whether a construct is meaningful.
 *
 * Everything is arena-allocated (ast/arena.h); nothing is freed individually.
 * Strings are NUL-terminated for convenience but always carry a length,
 * because content may contain NUL-free arbitrary bytes.
 */

struct wl_arena;

struct wl_span {
    size_t start; /* byte offset, inclusive */
    size_t end;   /* byte offset, exclusive */
};

struct wl_str {
    const char *data;
    size_t len;
};

enum wl_wp_kind {
    WL_WP_LITERAL, /* quote-removed text; `quoted` says where it came from */
    WL_WP_PARAM,   /* $name, ${name:-word}, $1, $@ ...                  */
    WL_WP_CMDSUB,  /* $(list) or `list`                                  */
    WL_WP_ARITH,   /* $((expr)): stored as source, evaluated in M3       */
    WL_WP_TILDE    /* ~ or ~user at the start of an unquoted word        */
};

enum wl_param_op {
    WL_PARAM_NONE = 0,
    WL_PARAM_DEFAULT,        /* ${x-word} / ${x:-word}    */
    WL_PARAM_ASSIGN,         /* ${x=word} / ${x:=word}    */
    WL_PARAM_ERROR,          /* ${x?word} / ${x:?word}    */
    WL_PARAM_ALTERNATE,      /* ${x+word} / ${x:+word}    */
    WL_PARAM_TRIM_PREFIX,    /* ${x#pattern}              */
    WL_PARAM_TRIM_PREFIX_LONG,
    WL_PARAM_TRIM_SUFFIX,    /* ${x%pattern}              */
    WL_PARAM_TRIM_SUFFIX_LONG,
    WL_PARAM_LENGTH          /* ${#x}                     */
};

struct wl_word;
struct wl_node;

struct wl_param {
    struct wl_str name; /* empty when index or special is used */
    int index;          /* 0..9, or -1                        */
    char special;       /* '@','*','?','$','#','!','-' or 0  */
    enum wl_param_op op;
    bool colon;         /* the ':' form of -/=/ ?/+           */
    struct wl_word *word;
};

struct wl_word_part {
    enum wl_wp_kind kind;
    bool quoted;
    union {
        struct wl_str literal;
        struct wl_param param;
        struct {
            struct wl_str source;       /* inner text, for diagnostics */
            struct wl_node *program;    /* parsed by the parser         */
        } cmdsub;
        struct {
            struct wl_str source;
        } arith;
        struct wl_str tilde;
    } u;
};

struct wl_word {
    struct wl_word_part *parts;
    size_t count;
    struct wl_span span;
    bool quoted; /* any part was quoted */
};

enum wl_redir_op {
    WL_REDIR_IN,           /* <   */
    WL_REDIR_OUT,          /* >   */
    WL_REDIR_APPEND,       /* >>  */
    WL_REDIR_READWRITE,    /* <>  */
    WL_REDIR_CLOBBER,      /* >|  */
    WL_REDIR_DUP_IN,       /* <&  */
    WL_REDIR_DUP_OUT,      /* >&  */
    WL_REDIR_HEREDOC,      /* <<  */
    WL_REDIR_HEREDOC_TABS, /* <<- */
    WL_REDIR_HERESTRING    /* <<< */
};

struct wl_heredoc {
    struct wl_str body;  /* raw text, no delimiter line, newlines kept */
    bool expand;         /* delimiter was unquoted                     */
    bool strip_tabs;     /* <<-                                        */
};

struct wl_redir {
    int fd; /* -1 when the operator carries no fd number */
    enum wl_redir_op op;
    struct wl_word *target;
    struct wl_heredoc *heredoc; /* NULL unless << or <<- */
    struct wl_span span;
};

enum wl_node_kind {
    WL_NODE_LIST,
    WL_NODE_ANDOR,
    WL_NODE_PIPELINE,
    WL_NODE_SIMPLE,
    WL_NODE_SUBSHELL,
    WL_NODE_GROUP,
    WL_NODE_FOR,
    WL_NODE_WHILE,
    WL_NODE_IF,
    WL_NODE_CASE,
    WL_NODE_FUNC
};

enum wl_andor_op {
    WL_ANDOR_AND = 0,
    WL_ANDOR_OR
};

struct wl_andor_link {
    enum wl_andor_op op;
    struct wl_node *node;
};

struct wl_if_arm {
    struct wl_node *condition;
    struct wl_node *body;
};

struct wl_case_item {
    struct wl_word **patterns;
    size_t pattern_count;
    struct wl_node *body;
    struct wl_span span;
};

struct wl_node {
    enum wl_node_kind kind;
    struct wl_span span;

    /* Redirections attached to this command (simple or compound). */
    struct wl_redir **redirs;
    size_t redir_count;

    union {
        struct {
            struct wl_node **statements; /* asynchronously run where true */
            bool *async;
            size_t count;
        } list;
        struct {
            struct wl_node *first;
            struct wl_andor_link *links;
            size_t count;
        } andor;
        struct {
            bool bang;
            struct wl_node **commands;
            size_t count;
        } pipeline;
        struct {
            struct wl_str *assign_names;
            struct wl_word **assign_values;
            size_t assign_count;
            struct wl_word **words;
            size_t word_count;
        } simple;
        struct {
            struct wl_node *body;
        } group; /* WL_NODE_SUBSHELL and WL_NODE_GROUP */
        struct {
            struct wl_str name;
            struct wl_word **words; /* may be NULL when `in` was omitted */
            size_t word_count;
            bool words_given;
            struct wl_node *body;
        } forloop;
        struct {
            bool until;
            struct wl_node *condition;
            struct wl_node *body;
        } whileloop;
        struct {
            struct wl_if_arm *arms;
            size_t arm_count;
            struct wl_node *else_body;
        } ifclause;
        struct {
            struct wl_word *word;
            struct wl_case_item *items;
            size_t item_count;
        } caseclause;
        struct {
            struct wl_str name;
            struct wl_node *body;
        } func;
    } u;
};

/* Creates a zeroed node of the given kind. Returns NULL on allocation failure. */
struct wl_node *wl_ast_node(struct wl_arena *arena, enum wl_node_kind kind,
                            struct wl_span span);

/*
 * Concatenates a word's literal parts into out and returns true. Returns false
 * as soon as a part needs expansion, leaving out unspecified; the caller
 * must report "expansion is not implemented yet" (M2 behaviour).
 */
bool wl_word_literal(const struct wl_word *word, struct wl_buf *out);

/*
 * If word starts with an unquoted NAME=, splits it into name and value and
 * returns true. The value is a word in the same arena (possibly empty).
 */
bool wl_word_split_assignment(struct wl_arena *arena,
                              const struct wl_word *word,
                              struct wl_str *name, struct wl_word **value);

/* Stable, specified text dump used by tests and diagnostics. */
void wl_ast_dump(const struct wl_node *node, struct wl_buf *out);

#endif /* WL_AST_AST_H */
