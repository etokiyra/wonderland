# Wonderland

A Unix shell in C17 for Linux, with an optional narrative layer: a dark
fairy-tale world inhabited by personas who react to what happens in the shell.

**Status: Milestone 3b. Wonderland is a working shell for scripts and
interactive use of the implemented features.** Variables, expansion,
redirections, pipelines, `if`/`for`/`while`/`case`, functions, positional
parameters, traps, `set`, and script files work. Job control, history and the
line editor do not. Do not put it in `/etc/shells` and do not `chsh` to it.

The ordering principle is not decorative:

> A proper shell first, a living inhabited world second.

## What works today

```sh
$ ./build/wonderland
wonderland:~/Projects/wonderland $ greet() { printf 'hello %s\n' "$1"; }
wonderland:~/Projects/wonderland $ greet world
hello world
wonderland:~/Projects/wonderland $ for f in a b c; do echo "$f"; done
a
b
c
wonderland:~/Projects/wonderland $ if test -f /etc/passwd; then echo present; fi
present
wonderland:~/Projects/wonderland $ printf 'a\nb\n' | wc -l
2
wonderland:~/Projects/wonderland $ cat <<EOF
> the world remembers
> EOF
the world remembers
```

* Interactive prompt on stderr, with `$?` shown when nonzero; Ctrl-C returns
  to a fresh prompt; terminal restored.
* External commands via `fork`/`execve`, `$PATH` lookup, 127/126/128+N status
  mapping, per-stage process groups, pipelines of any length.
* Variables and `export`; `$x`, `${x}`, `${x:-word}`, `${x#pat}`, `${#x}`,
  `$(...)`, `$((...))`, `~`, quote removal, `IFS` splitting, globbing.
* Redirections: `<`, `>`, `>>`, `<>`, `>|`, `2>&1`, `>&-`, here-documents,
  here-strings, applied left to right.
* Control flow: `if`/`elif`/`else`, `while`, `until`, `for`, `case`; `break`,
  `continue`, `return`; groups and real subshells.
* Functions with positional parameters; `$0`, `$1…`, `$@`, `$*`, `$#`,
  `shift`, `set --`.
* `trap` on `EXIT`, `INT`, `TERM`; `set -e/-u/-x/-n` (documented
  approximations).
* Script files: `wonderland script.sh arg...` with `$1…` set; `-c 'cmd' name
  args` follows the POSIX `$0` convention.
* Builtins: `:`, `break`, `cd`, `continue`, `exit`, `export`, `return`,
  `set`, `shift`, `trap`, `unset`.
* `--plain` / `WONDERLAND_PLAIN=1`; the narrative layer is not built yet, so
  plain mode currently has nothing to suppress.
* A parser for the whole grammar in `docs/GRAMMAR.md` with caret diagnostics.

## What does not work yet

| Missing | Arrives |
|---|---|
| Background jobs (`cmd &`), `jobs`, `fg`, `bg`, Ctrl-Z | M4 |
| History, line editing, completion, prompt configuration | M5 |
| `local`, `read`, `exec`, `command`, `type`, `wait`, `getopts`, `umask` | later |
| Aliases | opt-in, if ever |
| Personas, packs, `possess`, `rest`, `go-to-alice` | M3.5, M6–M7 |

Known sharp edges (D-0076): `"$@"` adjacent to other text joins instead of
splitting; non-whitespace `IFS` collapses repeated delimiters; INT/TERM traps
run between commands rather than asynchronously; `set -e` implements the common
suppression rule, not every POSIX corner; Ctrl-Z is a tested no-op until M4.

## Build and test

```sh
make                 # build/wonderland
make test            # 72 unit cases + 130 CLI checks + pty + 14 differential cases
make diff            # differential cases only (dash + bash required)
make asan            # the same under ASan/UBSan
make memcheck        # unit suite under valgrind (if installed)
make lint            # cppcheck over src/ (if installed)
```

Requirements: a C17 compiler, GNU Make, a POSIX shell. Optional developer
tools: `valgrind`, `cppcheck`, `dash` (required for `make diff`).

## Layout

```
src/core       shell state, variables, positional frames, options, buffers
src/term       terminal snapshot/restore, prompt formatting
src/line       canonical-mode reader (the M5 editor replaces it)
src/lex        lexer, word parts, quoting, here-document bodies
src/parse      parser, here-document queue, diagnostics
src/ast        arena, AST nodes, stable dump
src/expand     expansion, arithmetic, splitting, globbing
src/exec       AST execution, redirections, pipelines, functions, traps
src/jobs       signal dispositions, later the job table
src/builtins   cd, exit, export, unset, set, shift, trap, return, break...
src/narrative  personas and events (isolated library, M3.5+)
src/content    content pack loader (M6)
include/       public headers
tests/         unit, CLI, pty, differential cases, isolation probe
packs/         example content packs (original lines only)
scripts/       repository checks
docs/          design, architecture, grammar, personas, compatibility, ...
```

## Reading order

1. `docs/DESIGN.md` — what this is and what it refuses to be.
2. `docs/ARCHITECTURE.md` — subsystems, ownership, narrative isolation.
3. `docs/DECISIONS.md` — every decision, numbered, with open questions.
4. `docs/ROADMAP.md` — milestone order and the next piece of work.
5. `docs/GRAMMAR.md`, `docs/CONTENT_FORMAT.md`, `docs/PERSONAS.md` — the three
   specifications that are expensive to change later.
6. `docs/original-brief.md` — the source brief, verbatim.

`AGENTS.md` is the working contract: safety rules, style, ownership, commit
conventions, and the per-milestone workflow.

## Plain mode, stdout purity

`--plain`, `WONDERLAND_PLAIN=1`, `-c`, a script and stdin are all plain.
Narrative output, when it exists, will go to stderr or `/dev/tty` and never to
stdout. The prompt is shell presentation: stderr, suppressed when not
interactive. `tests/run.sh` captures stdout and stderr separately and asserts
byte-exact stdout for every execution path.

## Content

Wonderland distinguishes canonical material, sourced quotations, and original
writing. The loader (M6) refuses to treat unverified text as canonical. This
repository ships only original example lines; Black Souls dialogue and
third-party quotations are not committed here.

## Licence

No `LICENSE` file yet. The choice is an open decision
(`docs/DECISIONS.md` OQ-1) and is deliberately not guessed at.
