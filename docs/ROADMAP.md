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

The M1 line reader is a `getline()` loop plus a deliberately disposable
whitespace splitter. It must be marked `/* M1-ONLY: replaced by lex/parse in
M2 */` and must not grow features. If it starts parsing quotes, stop and move
to M2.

Exit criteria:

* `wonderland` inside a terminal runs `ls`, `cd /tmp`, `exit`; prompt shows
  `$?` and cwd basename.
* Terminal is restored after `exit`, Ctrl-D, and a deliberate `SIGINT` at the
  prompt (verified by reading `stty -a` in the parent).
* `wonderland -c 'true'`-style execution is still rejected (M3) but accepted
  as an option and reported honestly as unimplemented.
* Unit tests for option parsing, `PATH` search, status mapping; a `pty` test
  for start/exit.

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

## Recommended scope for the Milestone 1 prompt

> Implement Milestone 1 only, per `docs/ROADMAP.md` and `AGENTS.md`:
> (1) `src/term/` terminal snapshot/restore with an `atexit` + signal path and
> a `pty` test for start/exit; (2) a main loop that reads a line with
> `getline()` (interactive and `-c`, script file deferred to M3), prints a
> practical prompt with cwd basename and `$?`, and exits on Ctrl-D; (3) `cd`
> and `exit` builtins with POSIX exit statuses; (4) external execution via
> `fork`/`execvp` with `PATH` search and status mapping (127 not found, 126
> not executable); (5) the disposable whitespace splitter, clearly marked
> M1-ONLY, with zero quoting/expansion logic; (6) unit tests for option
> parsing and `PATH` resolution and a `pty` smoke test; (7) update
> `docs/DECISIONS.md` and report known limitations. Do not start the lexer,
> do not add history, do not add narrative code, do not touch `/etc/shells`.
