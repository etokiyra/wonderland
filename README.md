# Wonderland

A Unix shell in C17 for Linux, with an optional narrative layer: a dark
fairy-tale world inhabited by personas who react to what happens in the shell.

**Status: Milestone 2. The parser is real; the execution model is still
minimal.** Quoting, `;`, `&&`, `||`, `!` and comments work. Expansion,
redirection, pipelines, compound execution and background jobs are parsed but
**refused with status 2**. No job control, no history. Do not put it in
`/etc/shells` and do not `chsh` to it.

The ordering principle is not decorative:

> A proper shell first, a living inhabited world second.

## What works today

```sh
$ ./build/wonderland
wonderland:~/Projects/wonderland $ printf '[%s]\n' "a b"
[a b]
wonderland:~/Projects/wonderland $ echo one; echo two
one
two
wonderland:~/Projects/wonderland $ false && echo no; echo yes
yes
wonderland:~/Projects/wonderland $ echo $HOME
wonderland: expansion is not implemented yet (M3)
wonderland:~/Projects/wonderland [2] $ exit 3
```

* Interactive prompt on stderr, with `$?` shown when nonzero.
* External commands via `fork`/`execve`, `$PATH` lookup, 127/126/128+N status
  mapping, foreground process-group handoff, Ctrl-C at the prompt returns to a
  fresh prompt instead of killing the shell.
* Builtins: `cd` (HOME, `-`, PWD/OLDPWD) and `exit` (0–255).
* A real lexer and parser: single/double quotes, backslash escapes, empty
  quoted arguments, comments, line continuation, `;`, `&&`, `||`, `!`,
  assignments, redirections, here-documents, pipelines, `if`/`for`/`while`/
  `case`/functions, `${...}` operators — all parsed into an AST with caret
  diagnostics, and all tested. Only the literal-word subset executes.
* `-c STRING`, commands from stdin, and `-` for explicit stdin. All three are
  non-interactive and therefore plain. Multi-line `-c` and here-documents are
  parsed as one source unit.
* `--plain` and `WONDERLAND_PLAIN=1`; the narrative layer is not built yet, so
  plain mode currently has nothing to suppress, and the tests assert that
  stdout stays clean anyway.

## What does not work yet

| Missing | Arrives |
|---|---|
| Expansion (`$x`, `$(...)`, `$((...))`), splitting, globbing, tilde | M3 |
| Executing redirections, here-documents, pipes | M3 |
| Executing `if`/`for`/`while`/`case`, functions, assignments | M3 |
| Background jobs (`cmd &`) | M3/M4 |
| Script files (`wonderland file.sh`) — refused with status 3 | M3 |
| Job control (`jobs`, `fg`, `bg`, Ctrl-Z), history, line editing | M4–M5 |
| `ENOEXEC` fallback for shebang-less scripts | M8 |
| Personas, packs, `possess`, `rest`, `go-to-alice` | M3.5, M6–M7 |

Consequences today: `echo $HOME` is a clean refusal, `echo "a b"` is one
argument, `a;b` is two commands, and `ls | wc` parses but is refused. Ctrl-Z
does nothing to a running command (it is ignored, tested, and documented in
`docs/DECISIONS.md` D-0051) so that the shell can never wedge on a stopped
child.

## Build and test

```sh
make                 # build/wonderland
make test            # 65 unit cases + 77 CLI checks + a pty session test
make asan            # the same under ASan/UBSan
make memcheck        # unit suite under valgrind (if installed)
make lint            # cppcheck over src/ (if installed)
```

Requirements: a C17 compiler, GNU Make, a POSIX shell. Optional developer
tools: `valgrind`, `cppcheck`, `dash` (for the future differential suite).

## Layout

```
src/core       shell state, options, buffers, diagnostics
src/term       terminal snapshot/restore, prompt formatting
src/line       canonical-mode reader (the M5 editor replaces it)
src/lex        lexer, word parts and quoting
src/parse      parser, here-document queue, diagnostics
src/ast        arena, AST nodes, stable dump
src/expand     expansion, splitting, globbing (M3)
src/exec       PATH resolution, fork/exec, AST execution, builtin dispatch
src/jobs       signal dispositions, later the job table
src/builtins   cd, exit, later possess/rest/go-to-alice
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
