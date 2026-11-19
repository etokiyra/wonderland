# Wonderland — Grammar and AST

Status: proposal for Milestone 2. Nothing here is implemented yet.

The grammar is a POSIX sh command language with the quoting rules made
explicit, so that expansion can distinguish quoted from unquoted content
without re-reading the source. This is the part of the shell that cannot be
retrofitted, so it is specified before any code exists.

## 1. Layers

```
bytes → [lexer] → tokens (kind, span, quoting flags)
      → [parser] → AST (words made of word parts)
      → [expand] → argv + redirections (fields, already split and globbed)
      → [exec]   → status
```

The lexer is *not* a string splitter with extra states. It emits tokens whose
text is the exact source slice, plus flags describing how the slice was
quoted. The parser never inspects raw bytes except inside here-document bodies
and case patterns, which it hands to the lexer as sub-sources.

## 2. Lexical rules

### 2.1 Tokens

| Kind | Text | Notes |
|------|------|-------|
| `TOK_WORD` | source slice | may contain quotes, expansions, escapes |
| `TOK_ASSIGNMENT` | `name=value` slice | recognised only before the first word of a simple command; parser re-checks position |
| `TOK_IO_NUMBER` | digits | only when immediately followed by `<` or `>` with no space |
| `TOK_PIPE` | `\|` | |
| `TOK_AND_IF` | `&&` | |
| `TOK_OR_IF` | `\|\|` | |
| `TOK_SEMI` | `;` | |
| `TOK_DSEMI` | `;;` | case item terminator only; `;&`/`;;&` are not tokens |
| `TOK_AMP` | `&` | |
| `TOK_NEWLINE` | `\n` | |
| `TOK_LPAREN`, `TOK_RPAREN` | `(` `)` | |
| `TOK_LESS`, `TOK_GREAT`, `TOK_DGREAT`, `TOK_LESSGREAT`, `TOK_CLOBBER`, `TOK_DLESS`, `TOK_DLESSDASH`, `TOK_TLESS`, `TOK_LESSAND`, `TOK_GREATAND` | redirection operators | longest match first: `<<-` before `<<`, `>>` before `>`, `>&` before `>` |
| `TOK_EOF` | empty | always appended; position is one past the last byte |

Reserved words (`if then else elif fi do done case esac while until for in
function { } !`) are **words**, not token kinds. The parser classifies a
`TOK_WORD` as reserved only when (a) its text is unquoted and contains no
expansion, and (b) it appears in a position where a reserved word is legal
(command start, or the position after `case WORD in`, `for NAME`, etc.). This
is what makes `echo if`, `"if"`, and `\if` behave like ordinary arguments.

### 2.2 Operator recognition

An operator character is an operator only when it is **unquoted and
unescaped**. `a|b` is a pipeline; `a\|b`, `a'|'b`, `a"|"b` are single words
containing `|`. The lexer tracks, per byte, whether it was produced by a
quoted context or a backslash, and emits a per-word quote map alongside the
slice: bit `i` set = byte `i` is quoted/escaped. Expansion consumes the map,
so `*` globs only when its bit is clear and field splitting never splits on a
quoted byte.

### 2.3 Word parts

A word is lexed into parts at the moments quoting or expansion begins and
ends. This is the structure that preserves quoted vs unquoted content:

```c
enum wl_wp_kind {
    WL_WP_LITERAL,   /* bytes, with a quoted flag                     */
    WL_WP_PARAM,     /* $name, ${name}, ${name:-w}, $1, $@, $* ...    */
    WL_WP_CMDSUB,    /* $(list) — parsed recursively                  */
    WL_WP_ARITH,     /* $((expr)) — parsed later, M3                  */
    WL_WP_TILDE      /* ~ or ~user at word start, unquoted only      */
};

struct wl_word_part {
    enum wl_wp_kind kind;
    bool quoted;          /* part was inside "..." or fully escaped */
    union {
        struct { const char *text; size_t len; } literal;
        struct wl_param { const char *name; size_t name_len;
                          int index;              /* positional */
                          enum wl_param_op op;    /* :-, -, :=, :?, :+, #, ##, %, %% */
                          struct wl_word *word;   /* operand, if any */
                          bool colon; } param;
        struct wl_node  *cmdsub;                  /* N_LIST */
        struct wl_arith *arith;
        struct { const char *user; size_t len; } tilde;
    } u;
};

struct wl_word {
    struct wl_word_part *parts;  size_t nparts;
    struct wl_span span;
    bool has_quoted;   /* any part quoted: word can still split if others are not */
    bool from_assignment; /* NAME=value: no splitting or globbing on the value */
};
```

Invariants:

* Quote removal is not a parse-time mutation. `"a b"` yields one `LITERAL`
  part with `quoted = true`; splitting later sees `quoted` and refuses to
  split inside it.
* `""` and `''` yield a zero-length quoted `LITERAL` part. This is what keeps
  `cmd ""` passing one empty argument rather than none. A word that expands to
  nothing may still survive as an empty field if `has_quoted`.
* `$@` inside double quotes is a single part with `quoted = true` and a
  "splits into fields" flag; unquoted `$@` behaves like `$*` for splitting
  purposes but differs on empty arguments. These two cases get dedicated unit
  tests before implementation is considered done.
* Adjacent parts with equal `quoted` values are *not* merged during lexing;
  merging is an expansion-time optimization only. Keeping the boundaries makes
  diagnostics and dump tests readable.

### 2.4 Expansions recognized at lex time

`$name`, `${...}`, `$(...)`, `$((...))`, `$'...'` (Bash, deferred),
`` `...` `` (legacy, parsed but marked deprecated), `\` escapes per POSIX
(inside double quotes only `$`, `` ` ``, `"`, `\`, newline are special).

`$(...)` is scanned with paren counting that is quote-aware; nesting depth is
bounded (see §7).

## 3. Grammar (EBNF)

Terminal symbols are quoted; `{ x }` is repetition, `[ x ]` optional,
`( a | b )` alternation.

```
program      ::= list EOF
list         ::= and_or { separator_op and_or } [ separator_op ] [ comment_or_nl ]
separator_op ::= '&' | ';'
and_or       ::= pipeline { ( '&&' | '||' ) newline_list pipeline }
pipeline     ::= [ '!' ] command { '|' newline_list command }
command      ::= simple_command
               | compound_command [ redirect_list ]
               | function_definition
compound_command
             ::= brace_group | subshell | for_clause | case_clause
               | if_clause | while_clause | until_clause
brace_group  ::= '{' compound_list '}'
subshell     ::= '(' compound_list ')'
for_clause   ::= 'for' NAME [ [ 'in' [ word_list ] ] sequential_sep ]
                 do_group
while_clause ::= 'while' compound_list do_group
until_clause ::= 'until' compound_list do_group
do_group     ::= 'do' compound_list 'done'
if_clause    ::= 'if' compound_list 'then' compound_list
                 { 'elif' compound_list 'then' compound_list }
                 [ 'else' compound_list ] 'fi'
case_clause  ::= 'case' word newline_list 'in'
                 [ case_item { case_item } ] 'esac'
case_item    ::= [ '(' ] pattern [ '|' pattern ] { '|' pattern } ')' compound_list
                 ( ';;' | ';&' | ';;&' )     /* ;& and ;;& are Bash, off by default */
function_definition
             ::= NAME '(' ')' newline_list function_body
function_body::= compound_command [ redirect_list ]
simple_command
             ::= { assignment } [ redirect ] { word | redirect }
                 /* at least one word or assignment or redirect */
assignment   ::= NAME '=' word_value
word_value   ::= /* empty */ | word            /* no field splitting or globbing */
redirect     ::= [ io_number ] redirect_op word
redirect_op  ::= '<' | '>' | '>>' | '<>' | '>|' | '<&' | '>&'
               | '<<' | '<<-' | '<<<'
```

### 3.1 Notes that matter for correctness

* **Assignments only before the first word.** `a=1 b=2 cmd` sets both in the
  command environment. `cmd a=1` is two arguments.
* **Redirections may appear anywhere** in a simple command and are applied
  left to right, before the command runs.
* **`&&`/`||` are left-associative**; `;` and `&` have equal, lower precedence
  and `&` also terminates the list asynchronously.
* **A pipeline's exit status** is the last command's, unless `set -o pipefail`
  (must be explicit; off by default and only if implemented and tested).
* **Newlines are command separators** except after `&&`, `||`, `|`, `(`,
  `{`, `then`, `do`, `else`, `elif`, `in`, and inside quoted strings and
  here-doc bodies.

## 4. AST

```c
enum wl_node_kind {
    N_LIST,        /* stmts[], async[] (parallel array: stmt i is followed by '&' if async[i]) */
    N_ANDOR,       /* first, {op, node}[] */
    N_PIPELINE,    /* bang, nodes[] */
    N_SIMPLE,      /* assigns[], words[], redirs[] */
    N_SUBSHELL,    /* body */
    N_GROUP,       /* body */
    N_FOR,         /* var, words[] (may be NULL = "$@"), body */
    N_WHILE,       /* cond, body, until */
    N_IF,          /* cond, then, elif[], else */
    N_CASE,        /* word, items[] */
    N_CASE_ITEM,   /* patterns[], body, terminator */
    N_FUNC         /* name, body */
};

struct wl_node {
    enum wl_node_kind kind;
    struct wl_span span;              /* first byte .. one past last */
    union { /* one struct per kind, see ast/ast.h */ } u;
};

struct wl_redir {
    int fd;                 /* -1 = default for the operator */
    enum wl_redir_op op;
    struct wl_word *target; /* filename or fd number or here-doc delimiter */
    struct wl_heredoc *heredoc; /* non-NULL only for << and <<- */
    struct wl_span span;
};

struct wl_heredoc {
    struct wl_str body;   /* raw bytes after the newline, newlines kept */
    bool expand;          /* delimiter was unquoted */
    bool strip_tabs;      /* <<- */
};

/* Implemented form (docs/DECISIONS.md D-0052). */
struct wl_span { size_t start, end; };
```

* Spans are **byte offsets only**. Line and column are computed by
  `wl_diag_locate()` when a diagnostic is printed, because that is the only
  consumer and keeping them accurate through nested sub-parses would require a
  line map per source (D-0052). Offsets are what snippet rendering and
  here-document slicing need.
* Here-document bodies are stored raw with an `expand` flag, not as word parts
  (D-0054). M3 expands the body as a single word when the redirection is
  applied; a quoted delimiter means no expansion and `<<-` strips leading tabs
  at collect time.
* Nodes are arena-allocated (`ast/arena.h`) and freed only when the whole line
  is discarded. There is no per-node `free`.
* The AST is pure syntax. Assignment-vs-word decisions, reserved-word
  classification and here-doc collection happen during parsing; nothing in the
  AST refers to runtime state.
* `N_SIMPLE` keeps the source order of redirections and words; the expansion
  engine materializes argv, and `exec` applies redirections in order.

## 5. Here-documents

* The parser queues pending here-documents when it sees `<<`/`<<-`.
* At the next unquoted newline it reads bodies in queue order from the same
  token source: the body is every byte up to a line that is exactly the
  delimiter (after tab stripping for `<<-`), delimiter line excluded.
* Quoting any part of the delimiter (`<<"EOF"`, `<<\EOF`, `<<'EOF'`) makes the
  body literal: no expansion, no backslash processing.
* Unquoted delimiters mark the body `expand`; the body is stored as raw bytes
  and expanded as a single word when the redirection is applied (M3), never
  field split or globbed.
* End of input before the delimiter is a diagnostic marked `incomplete`
  (`here-document delimited by end of file`), so the interactive loop asks for
  more input and a script reports a syntax error with status 2.
* Interactive collection: the parser reports `incomplete` and the loop
  re-parses the appended logical line. There is no streaming source object in
  M2; the whole logical line lives in a fixed 4095-byte buffer (D-0056).

## 6. Redirections

| Operator | Meaning | Default fd |
|----------|---------|------------|
| `< word` | open for reading | 0 |
| `> word` | create/truncate for writing | 1 |
| `>> word` | append | 1 |
| `<> word` | open read/write | 0 |
| `>\| word` | truncate even with `noclobber` | 1 |
| `>& word` | duplicate output fd, or `>&-` to close | 1 |
| `<& word` | duplicate input fd, or `<&-` to close | 0 |
| `<< delim` | here-document | 0 |
| `<<- delim` | here-document, leading tabs stripped | 0 |
| `<<< word` | here-string (Bash, M8) | 0 |

Expansion rules for targets: the target of a redirection is expanded, but it
must produce exactly one field; `ambiguous redirect` is an error and the
command is not run. `>&3` and `<&0` take an fd number, not a filename; `>&-`
closes.

Application order: redirections are applied after the fork in the child, left
to right, each with `dup2`; a failure prints a diagnostic in the child and
exits 1 without running the command.

## 7. Limits and failure behaviour

| Limit | Value | Rationale |
|-------|-------|-----------|
| Nesting depth (parse recursion) | 200 | recursive descent must not overflow the stack on adversarial input |
| `$( )` / `${...}` nesting | 64 | the lexer's word-depth guard; the parser's nesting limit also applies |
| Interactive logical line | 4095 bytes | beyond this, refuse with a diagnostic and a fresh prompt |
| `-c` / stdin source | 1 MiB | read/parse cap (`WL_SOURCE_MAX`); refused before parsing |
| Command substitution source | bounded by the line/source cap | no separate limit in M2 |

All limits produce a diagnostic with a span, never a crash and never a silent
truncation. Exceeding a limit aborts the line (interactive) or the script
with status 2.

## 8. Diagnostics

```
wonderland: syntax error at line 3, column 12: expected 'fi' to close 'if' at line 1
    if true; then echo yes
               ^
```

* Positions are byte columns, 1-based. This is documented because it differs
  from editor columns in UTF-8 text.
* One diagnostic per parse (fail-fast), matching `sh` behaviour; batching is
  explicitly a non-goal (the brief does not ask for it, and the AST is
  discarded on error anyway).
* `wl_ast_dump()` exists for tests only; its output is a stable, specified text
  format, because tests compare it byte for byte.

## 9. Explicit non-goals in this grammar

* `[[ ]]`, arrays, associative arrays, process substitution, `coproc` are not
  in the v1 grammar. They are Bash extensions scheduled for after M8 and are
  listed as such in `docs/COMPATIBILITY.md`.
* No arithmetic *evaluation* in M2; `$((...))` is lexed and stored, evaluated
  in M3 when the expression evaluator exists.
* No alias expansion before M5 (needs the editor/history around it), and it is
  opt-in if implemented at all.

## 10. Test obligations for this document

Every rule above that says "must" or "never" needs a case in `tests/cases/`:

1. Quoting matrix: `a|b`, `a\|b`, `'a|b'`, `"a|b"`, `a""b`, `""`, `''`.
2. Splitting: `x="a b"; cmd $x` vs `cmd "$x"` vs `cmd ""`.
3. `$@` / `$*` under all four quoting combinations, including zero arguments.
4. Assignment prefixes: `a=1 b=$a cmd` (uses old `$a`).
5. Redirection order: `cmd >f 2>&1` vs `cmd 2>&1 >f`.
6. Here-docs: quoted delimiter, `<<-` with tabs, missing delimiter, nested in
   a pipeline and in a `while` body.
7. `case` patterns with `|`, wildcards, and quoted pattern characters.
8. Reserved-word edge cases: `echo if`, `"if"`, `\if`, `if=1`.
9. Depth limit: 200 nested parens is a diagnostic, 201 is not a crash.
