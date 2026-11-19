# Wonderland

A Unix shell in C17 for Linux, with an optional narrative layer: a dark
fairy-tale world inhabited by personas who react to what happens in the shell.

**Status: Milestone 3.5 / 5. Wonderland is a working shell for scripts and
daily interactive use, with a first slice of its narrative layer.** Variables,
expansion, redirections, pipelines, control flow, functions, positional
parameters, traps, `set`, script files, job control, a raw-mode line editor,
history, and tab completion work. Alice can speak after a command in an
interactive session; `--plain` silences her. The pack loader and the rest of
the cast are M6. Do not put it in `/etc/shells` and do not `chsh` to it yet.

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
wonderland:~/Projects/wonderland $ sleep 5 &
[1] 4242
wonderland:~/Projects/wonderland $ jobs
[1] Running   sleep 5
wonderland:~/Projects/wonderland $ Ctrl-Z
[1]+ Stopped sleep 5
wonderland:~/Projects/wonderland $ bg
[1]+ sleep 5 &
```

* Interactive prompt on stderr, with `$?` shown when nonzero; Ctrl-C returns
  to a fresh prompt; Ctrl-Z suspends, `bg`/`fg` resume; terminal restored.
* External commands via `fork`/`execve`, `$PATH` lookup, 127/126/128+N status
  mapping, per-stage process groups, pipelines of any length.
* Variables and `export`; `$x`, `${x}`, `${x:-word}`, `${x#pat}`, `${#x}`,
  `$(...)`, `$((...))`, `~`, quote removal, `IFS` splitting, globbing.
* Redirections: `<`, `>`, `>>`, `<>`, `>|`, `2>&1`, `>&-`, here-documents,
  here-strings, applied left to right.
* Control flow: `if`/`elif`/`else`, `while`, `until`, `for`, `case`; `break`,
  `continue`, `return`; groups and real subshells.
* Job control: `cmd &`, `$!`, `jobs`, `fg`, `bg`, `wait`, `%n` specs, Ctrl-Z.
* Functions with positional parameters; `$0`, `$1…`, `$@`, `$*`, `$#`,
  `shift`, `set --`.
* `trap` on `EXIT`, `INT`, `TERM`; `set -e/-u/-x/-n` (documented
  approximations).
* Script files: `wonderland script.sh arg...` with `$1…` set; `-c 'cmd' name
  args` follows the POSIX `$0` convention.
* Interactive editing: arrows, Home/End/Delete, Ctrl-A/E/B/F/K/U/W/L/C/D,
  history recall (Up/Down), tab completion, `WONDERLAND_PROMPT`.
* History persisted at `$XDG_STATE_HOME/wonderland/history` (0600, bounded).
* `$'...'` ANSI-C quoting and a `read [-r]` builtin.
* Builtins: `:`, `bg`, `break`, `cd`, `continue`, `exit`, `export`, `fg`,
  `jobs`, `read`, `return`, `set`, `shift`, `trap`, `unset`, `wait`.
* Optional narrative slice: Alice may comment after a failed command (and
  rarely after successes) in an interactive session, on stderr only. Plain
  mode, `-c`, scripts and pipes silence her completely.
* A parser for the whole grammar in `docs/GRAMMAR.md` with caret diagnostics.

## What does not work yet

| Missing | Arrives |
|---|---|
| Pack loader, persona engine, `possess`, the rest of the cast | M6 |
| Alice's Library, `rest`, mirror, `go-to-alice` | M7 |
| `kill`, `disown`, `jobs -l`, warning on exit with stopped jobs | M8 polish |
| UTF-8 cursor movement, completion listing, Ctrl-R history search | M8 polish |
| `local`, `exec`, `command`, `type`, `getopts`, `umask` | later |
| Aliases | opt-in, if ever |

Known sharp edges (D-0076, D-0082, D-0089): `"$@"` adjacent to other text joins instead
of splitting; non-whitespace `IFS` collapses repeated delimiters; INT/TERM
traps and job notifications run between commands rather than asynchronously;
`set -e` implements the common suppression rule, not every POSIX corner;
background pipelines appear as one job rather than one entry per stage; the
editor is byte-oriented (no UTF-8 cursor movement) and completion does not list
candidates or quote paths with spaces.

## Build and test

```sh
make                 # build/wonderland
make test            # 79 unit cases + 143 CLI checks + pty + 14 differential cases
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
src/line       raw-mode editor, history, canonical reader used by read(1)
src/lex        lexer, word parts, quoting, here-document bodies
src/parse      parser, here-document queue, diagnostics
src/ast        arena, AST nodes, stable dump
src/expand     expansion, arithmetic, splitting, globbing
src/exec       AST execution, redirections, pipelines, functions, traps
src/jobs       signals, job table, waiting, terminal ownership
src/builtins   cd, exit, export, unset, set, shift, trap, return, break...
src/narrative  Alice slice: immutable events in, borrowed text out
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
