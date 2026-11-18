# Wonderland

A Unix shell in C17 for Linux, with an optional narrative layer: a dark
fairy-tale world inhabited by personas who react to what happens in the shell.

**Status: bootstrap. There is no shell yet.** This repository currently
contains the design documents, the build skeleton, a test harness, and a
binary that answers `--version`, accepts `--plain`, and refuses everything
else. Do not use it as a shell, do not put it in `/etc/shells`, and do not
`chsh` to it.

The ordering principle is not decorative:

> A proper shell first, a living inhabited world second.

## What exists

| Part | State |
|------|-------|
| Design and architecture docs | written (`docs/`) |
| Build system (`make`, `make test`, `make asan`) | working |
| Binary: `--version`, `--plain`, refusal of unsupported requests | working |
| C test harness and CLI tests | working |
| Narrative library boundary and isolation checks | working (placeholder code) |
| Shell: lexer, parser, execution, jobs, editor | **not started** |
| Content packs: loader | **not started**; one example pack with original lines |

## Build and test

```sh
make                 # build/wonderland
make test            # unit + CLI + isolation checks
make asan            # the same under ASan/UBSan
./build/wonderland --version
```

Requirements: a C17 compiler, GNU Make, a POSIX shell. No third-party
libraries. `dash` and `valgrind` are useful but not required (see
`docs/RISKS.md` R12).

## Try it

```sh
$ ./build/wonderland --version
wonderland 0.0.0 (bootstrap; no shell implemented)

$ ./build/wonderland
wonderland: the interactive shell is not implemented in this bootstrap build; see docs/ROADMAP.md.
$ echo $?
3
```

## Layout

```
src/core       shell state, variables, options, diagnostics, arenas
src/term       terminal snapshot/restore, window size, /dev/tty
src/line       line editor, history, completion
src/lex        lexer and tokens
src/parse      parser and diagnostics
src/ast        AST nodes and traversal
src/expand     parameter/command/arithmetic expansion, splitting, globbing
src/exec       builtin dispatch, fork/exec, pipelines, redirections
src/jobs       process groups, job table, signals
src/builtins   cd, exit, possess, rest, go-to-alice, ...
src/narrative  personas and events (isolated library)
src/content    content pack loader
include/       public headers (narrative headers are namespaced separately)
tests/         unit tests, isolation probe, black-box CLI tests
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

## Plain mode

`--plain`, or `WONDERLAND_PLAIN=1`, or any non-interactive invocation
(`-c`, a script file, a pipe) disables the narrative layer completely.
Narrative output, when it exists, goes to stderr or `/dev/tty` and never to
stdout. Plain mode currently has nothing to suppress; the tests assert that
nothing reaches stdout anyway.

## Content

Wonderland distinguishes canonical material, sourced quotations, and original
writing. The loader refuses to treat unverified text as canonical. This
repository ships only original example lines; Black Souls dialogue and
third-party quotations are not committed here.

## Licence

No `LICENSE` file yet. The choice is an open decision
(`docs/DECISIONS.md` OQ-1) and is deliberately not guessed at.
