# Wonderland — Roadmap

Status: M0 (this bootstrap) is the only completed milestone. Everything else
is planned work with exit criteria, not a wish list.

Amendment 7 changes the brief's order: a thin vertical slice of the narrative
layer lands immediately after execution (M3), before job control and the line
editor. The reason is sequencing risk: if the narrative layer is only wired in
at M6, every execution decision taken in M3–M5 is made without knowing whether
the isolation contract actually works. The slice makes the contract real while
it is still cheap to change.

## M0 — Bootstrap (done)

Deliverables: repository, docs, `AGENTS.md`, Makefile (`all`, `test`, `asan`,
`clean`), a binary that only handles `--version`/`--plain`, a dependency-free
C test harness, `tests/run.sh`, the PercyLang reuse audit.

Exit criteria: `make`, `make test`, `make asan` pass on a clean checkout. No
shell logic. ✅

## M1 — Foundation

Scope (from the brief, unchanged): project structure wired into the real
build, terminal initialisation and restoration, main loop, basic prompt,
`cd` and `exit` builtins, external command execution with `fork`/`exec`,
`PATH` lookup, status propagation.

Explicit non-scope: quoting, expansion, pipes, redirections, history, the
line editor, narrative.

The interactive reader is `read(2)` on the canonical tty, plus a deliberately
disposable whitespace splitter (`src/exec/m1_split.c`). The splitter is marked
M1-ONLY and must not grow features. If it starts parsing quotes, stop and move
to M2.

Status: **done** (2026-11-19). Evidence: `make test` runs 48 unit cases (116
assertions), 54 CLI checks, and a pty test covering prompt, external command,
`cd`, Ctrl-C at the prompt, `exit 4`, and unchanged termios. `make asan`,
`make memcheck` (valgrind) and `make lint` (cppcheck) are clean.

Deviations from the plan above, recorded in `docs/DECISIONS.md`:

* `-c` executes in M1 rather than M3 (D-0047); script files still refuse.
* Foreground process-group handoff is included early, because without it
  Ctrl-C kills the shell (D-0044).
* The prompt uses the full HOME-abbreviated path, not the basename (D-0046).
* SIGINT uses a `sig_atomic_t` flag until a poll loop exists (D-0045).

Exit criteria as met:

* `wonderland` in a terminal runs `ls`, `cd /tmp`, `exit`; the prompt shows
  `$?` when nonzero; Ctrl-C returns to a fresh prompt.
* Terminal settings are unchanged across a session, asserted by the pty test.
* `wonderland -c 'echo hi'` runs and propagates status; `cd`/`exit` behave as
  specified in D-0049.
* A pty test for start/command/Ctrl-C/exit exists and runs in `make test`.

## M2 — Lexer, Parser, AST

Scope: tokens with spans and quote maps, quoting, word parts, grammar from
`docs/GRAMMAR.md`, AST, diagnostics with carets, here-document parsing,
`wl_ast_dump()` for tests. No execution changes: the M1 path keeps working.

Exit criteria:

* The quoting matrix in `docs/GRAMMAR.md` §10 passes as unit tests.
* `wl_ast_dump()` output is stable and reviewed for 30 representative lines.
* Depth limits produce diagnostics, not crashes (fuzz with 10k random inputs
  under ASan).
* Parse error in interactive mode discards the line and returns to the prompt
  with the previous status intact.

## M3 — Execution

Scope: expansion (parameter, command substitution, arithmetic, tilde), field
splitting, globbing, quote removal, pipes, redirections, `&&`/`||`/`;`/`&`,
subshells, brace groups, functions, `set -e/-u/-x`.

Exit criteria: the POSIX surface in `docs/COMPATIBILITY.md` §3.1 M1–M3 rows is
green against bash and (when installed) dash. `yes | head` does not hang.
Redirection-order cases pass. No fd leaks: a 10k-command loop does not exhaust
descriptors.

## M3.5 — Narrative vertical slice (amendment 7)

Scope, and nothing more:

* `libwl_narrative.a` with the isolation rules from `docs/ARCHITECTURE.md`
  §3.1 (link probe + source audit + restricted include path).
* One persona, **Alice**, with **12 original lines** compiled into the
  library as a static table (no pack format yet — the format is M6's job and
  should not be frozen by a slice).
* A single hook: `post_exec`, fired after the status is stored.
* The plain-mode gate: `--plain`, `WONDERLAND_PLAIN=1`, non-interactive, or
  stdin/stderr not a tty ⇒ hook never called.
* Output to stderr only; no `/dev/tty` logic yet.

Exit criteria:

* `echo hi` on a tty prints nothing to stdout beyond `hi`; Alice's line (if
  selected) appears on stderr, verified by capturing streams separately.
* `wonderland script.sh` and `wonderland -c '...'` produce byte-identical
  stderr with the narrative library removed from the link.
* `make check-isolation` passes.
* The suite proves exit codes and stdout are unchanged with the narrative
  library linked and unlinked.

## M4 — Job Control

Scope: process groups, foreground/background jobs, `jobs`, `fg`, `bg`, `%n`,
`Ctrl-Z`, `SIGCHLD` via self-pipe, terminal ownership, `wait`, `trap` for
`INT`/`TERM`/`EXIT`.

Exit criteria: interactive matrix (start, suspend, resume, background, kill,
exit with stopped jobs) passes under a pty; no orphaned process groups
(`ps -o pgid` checks); terminal ownership returns to the shell after every
job; the shell works in an 80×24 SSH session.

## M5 — Interactive Experience

Scope: raw-mode editor, cursor movement, history (file persistence with
permissions), completion (commands, paths), resize handling, prompt
configuration, `$'...'` quoting, `printf`, `read`.

Exit criteria: editor survives resize storm, bracketed-paste off, a 10k-line
history, and a terminal closed mid-line; `stty sane` is never needed after a
normal exit; no input eaten when pasting.

## M6 — Narrative Foundation

Scope: content-pack loader per `docs/CONTENT_FORMAT.md`, persona records from
packs, event system with the enum in `docs/PERSONAS.md`, selection with
frequency/cooldowns/anti-repetition, `possess`, config keys, `--plain`
plumbing end to end, the remaining four wave-1 personas (12–40 lines each).

Exit criteria:

* A malformed pack yields one diagnostic line, the shell starts, other packs
  load (fuzz the loader with truncated/mutated packs under ASan).
* A pack claiming `canonical` without verification is rejected (test).
* 30-minute scripted session produces 0–2 lines per minute, no repeats within
  25 lines, no output on stdout, nothing at all in plain mode.
* Blind attribution test on 20 lines per wave-1 persona ≥ 80% (human gate).

## M7 — Wonderland Identity

Scope: Alice's Library cycle counter with atomic, versioned, checksummed
state; `rest`/`respawn` scoped exactly as `docs/PERSONAS.md` §8; death screen;
Black Rabbit presentation; mirror mode restricted to owned presentation;
`go-to-alice` sharing `exit` logic with optional confirmation.

Exit criteria: corrupt state file ⇒ fresh start with one note; `rest`/`respawn`
round-trip test; no themed mechanic changes any exit status (property test:
run a matrix with mechanics on/off and diff statuses).

## M8 — Compatibility and Polish

Scope: selected Bash extensions (`${var//a/b}`, `<<<`, `local`, `pipefail`,
`[[ ]]`, arrays last), broader test suite, memory/resource audit (ASan, leak
checks), documentation pass, installation instructions that keep the shell
*beside* bash, example packs, `make install` (prefix decision).

Exit criteria: extended matrix green; no known leaks; `/etc/shells` guidance
still says "not yet"; README claims match `docs/COMPATIBILITY.md`.

## Dogfooding gate (after M5)

Before M7 work starts: four weeks of daily interactive use launched by hand
from bash. Bugs found here outrank new features. The gate to `chsh` is in
`docs/COMPATIBILITY.md` §9 and is stricter still.

## Ordering rationale

* Terminal and process foundations before grammar: a parser is useless if the
  terminal is left raw on exit.
* Grammar before expansion: expansion is defined in terms of word parts.
* Execution before jobs: a pipeline is a special case of process creation.
* Narrative slice before jobs: cheap now, expensive to retrofit later.
* Packs before identity mechanics: `rest`/`respawn` persist narrative state,
  which must have a defined shape first.
* Bash extensions last: they are the most likely to distort the parser, and
  they are the least necessary for daily use.

## Milestone 1 as implemented

Delivered: `src/term/` snapshot/restore (+ `atexit`), `src/term/prompt.c`,
`src/jobs/signal.c`, `src/line/read.c`, `src/exec/{path,exec,m1_split}.c`,
`src/core/{shell,opts}.c`, `src/builtins/builtins.c` (`cd`, `exit`), the loop
and `-c`/stdin handling in `src/main.c`, and tests at three levels (unit, CLI,
pty). The `pty` test drives a real terminal, not a mock.

## Recommended scope for the Milestone 2 prompt

> Implement Milestone 2 only, per `docs/ROADMAP.md`, `docs/GRAMMAR.md` and
> `AGENTS.md`: (1) tokens with byte spans and a per-byte quote map, including
> the quoting matrix in `docs/GRAMMAR.md` §10; (2) word parts (`literal`,
> `param`, `cmdsub`, `arith`, `tilde`) that preserve quoted vs unquoted
> content, with `""` surviving as an empty quoted part; (3) the grammar's
> lists/pipelines/simple commands/reserved words/redirections/here-doc queue,
> with the documented recursion and size limits; (4) AST nodes in the per-line
> arena, `wl_ast_dump()` with a stable format; (5) diagnostics with
> `file:line:col`, a source excerpt and a caret, one error per line;
> (6) delete `src/exec/m1_split.c` and its header, and make execution consume
> the AST for simple commands only — no expansion, no pipes, no redirections
> yet beyond parsing them and refusing cleanly; (7) parser, lexer and dump
> tests plus an ASan fuzz pass over random inputs; (8) update
> `docs/DECISIONS.md`, `docs/COMPATIBILITY.md` and report known limitations.
> Do not implement expansion, job control, history, or narrative code.
