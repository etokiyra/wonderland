# Wonderland

A Unix shell in C17 for Linux, with an optional narrative layer: a dark
fairy-tale world inhabited by personas who react to what happens in the shell.

**Status: Milestone 3a. Words expand, redirections redirect, and pipelines
pipe.** Variables, `$...`, `$(...)`, `$((...))`, `IFS` splitting, globbing,
here-documents, `export`/`unset`, and multi-stage pipelines work. Control flow
(`if`/`for`/`while`/`case`), functions, job control and history do not. Do not
put it in `/etc/shells` and do not `chsh` to it.

The ordering principle is not decorative:

> A proper shell first, a living inhabited world second.

## What works today

```sh
$ ./build/wonderland
wonderland:~/Projects/wonderland $ X="a b"
wonderland:~/Projects/wonderland $ printf '[%s]\n' $X
[a]
[b]
wonderland:~/Projects/wonderland $ echo $(echo hi) $((6 * 7))
hi 42
wonderland:~/Projects/wonderland $ echo one | tr a-z A-Z
ONE
wonderland:~/Projects/wonderland $ cat <<EOF
> the world remembers
> EOF
the world remembers
```

* Interactive prompt on stderr, with `$?` shown when nonzero.
* External commands via `fork`/`execve`, `$PATH` lookup, 127/126/128+N status
  mapping, per-stage process groups, Ctrl-C at the prompt returns to a fresh
  prompt instead of killing the shell.
* Variables and `export`; `$x`, `${x}`, `${x:-word}`, `${x#pat}`, `${#x}`,
  `$(...)`, `$((...))`, `~`, quote removal, `IFS` splitting, globbing.
* Redirections: `<`, `>`, `>>`, `<>`, `>|`, `2>&1`, `>&-`, here-documents,
  here-strings, applied left to right.
* Pipelines of any length; `$?` is the last stage's status.
* `;`, `&&`, `||`, `!`, comments, line continuation, and continuation prompts.
* Builtins: `cd`, `exit`, `export`, `unset`.
* `-c`, commands from stdin, and `-`; all non-interactive and therefore plain.
* A parser for the *whole* grammar in `docs/GRAMMAR.md`, including compounds
  and functions, with caret diagnostics — even where execution is still M3b.

## What does not work yet

| Missing | Arrives |
|---|---|
| Executing `if`/`for`/`while`/`case`, functions, subshells, `{ }` | M3b |
| `return`, `break`, `continue`, `shift`, positional parameters | M3b |
| `trap`, `set -e/-u/-x`, `read`, `exec`, `command`, `wait` | M3b |
| Background jobs (`cmd &`) | M4 |
| Script files (`wonderland file.sh`) — refused with status 3 | M8 (parser is ready; wiring is small) |
| Job control (`jobs`, `fg`, `bg`, Ctrl-Z), history, line editing | M4–M5 |
| `ENOEXEC` fallback for shebang-less scripts | M8 |
| Personas, packs, `possess`, `rest`, `go-to-alice` | M3.5, M6–M7 |

Known sharp edges: `"$@"` has no positional parameters yet, so it yields one
empty argument rather than zero; non-whitespace `IFS` collapses repeated
delimiters. Both are recorded in `docs/DECISIONS.md` D-0068. Ctrl-Z does
nothing to a running command (ignored, tested) so the shell can never wedge on
a stopped child.

## Build and test

```sh
make                 # build/wonderland
make test            # 72 unit cases + 101 CLI checks + a pty session test
make asan            # the same under ASan/UBSan
make memcheck        # unit suite under valgrind (if installed)
make lint            # cppcheck over src/ (if installed)
```

Requirements: a C17 compiler, GNU Make, a POSIX shell. Optional developer
tools: `valgrind`, `cppcheck`, `dash` (for the future differential suite).

## Layout

```
src/core       shell state, variables, options, buffers, diagnostics
src/term       terminal snapshot/restore, prompt formatting
src/line       canonical-mode reader (the M5 editor replaces it)
src/lex        lexer, word parts, quoting, here-document bodies
src/parse      parser, here-document queue, diagnostics
src/ast        arena, AST nodes, stable dump
src/expand     expansion, arithmetic, splitting, globbing
src/exec       PATH resolution, fork/exec, redirections, pipelines
src/jobs       signal dispositions, later the job table
src/builtins   cd, exit, export, unset; later possess/rest/go-to-alice
src/narrative  personas and events (isolated library, M3.5+)
src/content    content pack loader (M6)
include/       public headers
tests/         unit, CLI, pty, isolation probe
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
