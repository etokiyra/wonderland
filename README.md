# Wonderland

A Unix shell in C17 for Linux, with an optional narrative layer: a dark
fairy-tale world inhabited by personas who react to what happens in the shell.

**Status: Milestone 7. Wonderland is a working shell for scripts and daily
interactive use, with an inhabited narrative layer and persistent identity.**
Variables, expansion, redirections, pipelines, control flow, functions,
positional parameters, traps, `set`, script files, job control, a raw-mode
line editor, history and completion work. Five personas (Alice, Grimm,
Nietzsche, Kafka, Cioran) live in content packs and react to commands;
`possess` switches between them, Alice's Library counts sessions, and
`rest`/`respawn` checkpoint shell state. `--plain` silences the world and
never touches the state file. Do not put it in `/etc/shells` and do not
`chsh` to it yet.

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
* Styled prompt: colour, Nerd Font icons, git branch, active-job count and
  the previous command's duration. `WONDERLAND_COLOR`, `WONDERLAND_ICONS`,
  `WONDERLAND_GIT` and `WONDERLAND_DURATION` toggle each part; `NO_COLOR`
  and `TERM=dumb` are honoured, and a plain ASCII prompt remains available.
* History persisted at `$XDG_STATE_HOME/wonderland/history` (0600, bounded).
* `$'...'` ANSI-C quoting and a `read [-r]` builtin.
* Builtins: `:`, `bg`, `break`, `cd`, `continue`, `exit`, `export`, `fg`,
  `go-to-alice`, `jobs`, `possess`, `read`, `respawn`, `rest`, `return`,
  `set`, `shift`, `trap`, `unset`, `wait`.
* Narrative layer: five personas with original lines, reacting to failures,
  successes, directory changes, jobs and session start/exit, on stderr only.
  `possess -l` lists them, `possess grimm` switches. Alice's Library persists
  the session cycle and active persona; `rest` checkpoints cwd, a whitelisted
  variable set and the persona; `respawn` restores those and says plainly that
  processes are not resurrected. Plain mode, `-c`, scripts and pipes silence
  the world completely (no packs or state are even loaded).
* A parser for the whole grammar in `docs/GRAMMAR.md` with caret diagnostics.

## What does not work yet

| Missing | Arrives |
|---|---|
| The rest of the cast (20–30 personas) and more lines per persona | M8+ |
| Config file for the `WONDERLAND_*` flags, richer mirror/death presentation | M8+ |
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
make test            # 106 unit cases + 154 CLI checks + pty + 14 differential cases
make diff            # differential cases only (dash + bash required)
make asan            # the same under ASan/UBSan
make memcheck        # unit suite under valgrind (if installed)
make lint            # cppcheck over src/ (if installed)
```

Requirements: a C17 compiler, GNU Make, a POSIX shell. Optional developer
tools: `valgrind`, `cppcheck`, `dash` (required for `make diff`).

## Install it to try it

```sh
make install        # installs ~/.local/bin/wonderland (PREFIX=... to override)
wonderland          # start it from your current shell
```

`~/.local/bin` must be on `PATH` (it is on this machine). The install target
does **not** touch `/etc/shells`, does not use `chsh`, and does not change your
login shell: Wonderland is just another command you start and leave with
`exit` or Ctrl-D. `make uninstall` removes the binary.

A few things worth trying in the first session:

* `echo hello` then press Up, then Ctrl-A and edit the line.
* `ech<TAB>` completes the command; Tab on paths completes files.
* `sleep 30 &`, then `jobs`, `fg`, Ctrl-Z, `jobs`, `bg`.
* `for i in 1 2 3; do echo $i; done`, a function with `$1`, and a here-doc.
* `false` and watch Alice answer; `wonderland --plain` keeps her quiet.
* `trap 'echo bye' EXIT; echo hi`, then exit.

The history file lands in `$XDG_STATE_HOME/wonderland/history` (mode 0600).

## Layout

```
src/core       shell state, variables, positional frames, options, buffers,
               Alice's Library state file (versioned, checksummed, atomic)
src/term       terminal snapshot/restore, prompt formatting
src/line       raw-mode editor, history, canonical reader used by read(1)
src/lex        lexer, word parts, quoting, here-document bodies
src/parse      parser, here-document queue, diagnostics
src/ast        arena, AST nodes, stable dump
src/expand     expansion, arithmetic, splitting, globbing
src/exec       AST execution, redirections, pipelines, functions, traps
src/jobs       signals, job table, waiting, terminal ownership
src/builtins   cd, exit, export, unset, set, shift, trap, return, break...
src/narrative  persona engine: events in, borrowed text out
src/content    content pack loader, validation, provenance
packs/core     five core personas (original lines only)
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
