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
`wl_ast_dump()` for tests.

Status: **done** (2026-11-19). Evidence: 65 unit cases (229 assertions,
including 10,000 deterministic fuzz inputs), 77 CLI checks, and a pty test
covering a trailed-backslash continuation and an open-quote continuation.
`make asan`, `make memcheck` and `make lint` are clean, with gcc and clang.

Delivered: `src/lex/{lex,word}.c`, `src/parse/parse.c`, `src/ast/{arena,ast,dump}.c`,
`src/core/{buf,diag}.c`, caret diagnostics, `WL_TOK_DSEMI`, here-document
bodies, command-substitution sub-programs, and interactive continuation with
a `> ` prompt. `src/exec/m1_split.c` is deleted; `src/exec/exec.c` now walks
the AST for the constructs listed in D-0057.

Deviations from the plan, all recorded:

* Spans are byte offsets; line/column is computed when a diagnostic prints
  (D-0052), not stored per node.
* Here-document bodies are stored raw with an `expand` flag and expanded in
  M3, rather than parsed into word parts now (D-0054).
* `;&`/`;;&` are rejected; `;;` is the only case terminator (D-0055).
* A syntax error sets `$?` to 2 (D-0056), replacing the "previous status"
  wording above.
* Execution support is intentionally narrow: `;`, `&&`, `||`, `!`, and
  literal words; everything else refuses with status 2 (D-0057).

Exit criteria as met:

* The quoting matrix in `docs/GRAMMAR.md` §10 is covered at parse level
  (`a|b`, `a\|b`, `'a|b'`, `"a|b"`, `a""b`, quoted vs unquoted parameters,
  `$@` quoting, empty quoted arguments).
* `wl_ast_dump()` output is fixed and asserted, including one whole-tree dump.
* Depth limits produce diagnostics, not crashes; 10,000 fuzz inputs run under
  ASan on every `make test`.
* A parse error in interactive mode prints a caret diagnostic, sets `$?` to 2,
  discards the line, and returns to the prompt.

## M3 — Execution

Scope: expansion (parameter, command substitution, arithmetic, tilde), field
splitting, globbing, quote removal, pipes, redirections, `&&`/`||`/`;`/`&`,
subshells, brace groups, functions, `set -e/-u/-x`.

### M3a — expansion and the process model: **done** (2026-11-19)

Delivered: `core/var.c` (variables + exported environment), `expand/expand.c`
(parameter/command/arithmetic/tilde expansion, quote removal, `IFS` splitting,
globbing), `expand/arith.c`, redirections (all operators, including
here-documents and here-strings), multi-stage pipelines with terminal
ownership, prefix/standalone assignments, `export`, `unset`, and command
substitution.

Evidence: 72 unit cases (277 assertions), 101 CLI checks, and a pty test that
runs a pipeline and a here-document through a real terminal. `make asan`,
`make memcheck` and `make lint` are clean with gcc and clang. Manual checks
against dash/bash covered `${x:-a b}` vs `${x:-"a b"}`, `cd` in a pipeline,
prefix environments, and `yes | head` (no hang).

Deviations and limits are recorded as D-0059…D-0068. In particular: no
positional parameters yet, non-whitespace `IFS` is approximate, and compound
commands are still refused.

### M3b — control flow and functions: **done** (2026-11-19)

Delivered: `if`/`elif`/`else`, `while`, `until`, `for`, `case`, `{ }`, `( )`,
function definition and calls, positional parameters, `shift`, `return`,
`break`, `continue`, `trap` on `EXIT`/`INT`/`TERM`, `set -e/-u/-x/-n`,
script-file execution with arguments, and the dash/bash differential harness
(14 cases).

Evidence: 72 unit cases (281 assertions), 130 CLI checks, the pty test, and
14/14 differential cases passing against both dash and bash. `make asan`,
`make memcheck` and `make lint` are clean with gcc and clang. A real
use-after-free in `set --` was found by ASan and fixed (D-0069).

Exit criteria: the redirection, expansion, control-flow, function and trap
cases are green against dash and bash; `yes | head` does not hang; the EXIT
trap runs once and preserves the status. `set -e` corner cases are covered by
the documented approximation (D-0074), not claimed as full POSIX.

## M3.5 — Narrative vertical slice (amendment 7)

Status: **done** (2026-11-19).

Delivered: `libwl_narrative.a` with Alice and **12 original lines** in a
static table; one `post_exec` hook fed a `const struct wl_event`; the persona
returns borrowed text and the shell writes it to stderr; the plain gate
(interactive && !plain; `-c`, scripts, stdin never speak); selection is
failure-always, every sixth success, and exit, with a four-line no-repeat ring.

Evidence: 79 unit cases (331 assertions, including cadence and no-repeat), 143
CLI checks (a failing `-c` leaves stderr empty), a pty session that proves
Alice speaks on `false`, and a second `--plain` pty session that proves she
stays silent. The narrative link probe and source audit still pass; ASan,
valgrind and cppcheck are clean with gcc and clang.

Deviations: no pack format (`alice.pack` remains an example for M6), and
deterministic cadence instead of the full frequency/cooldown model (D-0092).
The library still cannot touch execution state; that is the point of the
slice.

## M4 — Job Control

Scope: process groups, foreground/background jobs, `jobs`, `fg`, `bg`, `%n`,
`Ctrl-Z`, `SIGCHLD` via self-pipe, terminal ownership, `wait`, `trap` for
`INT`/`TERM`/`EXIT`.

Status: **done** (2026-11-19), with one deliberate deviation: there is no
SIGCHLD self-pipe. The shell is single-threaded and reaps between commands and
before each prompt (`waitpid(WNOHANG|WUNTRACED|WCONTINUED)`); foreground jobs
wait with `WUNTRACED`. Notifications therefore appear at the next prompt, not
mid-command (D-0080).

Delivered: `src/jobs/job.c` (process-group jobs, ids, waiting, notifications),
interactive job initialisation (`setpgid` + terminal ownership), background
`&` for any statement, `jobs`/`fg`/`bg`/`wait`, `%n`/`%%`/`%-` job specs,
Ctrl-Z suspend/resume, and `$!`.

Evidence: 72 unit cases, 138 CLI checks, the pty test now starts a background
job, lists it, waits, suspends a foreground job with Ctrl-Z, lists it as
Stopped and resumes it with `bg`; 14 differential cases still agree with dash
and bash. ASan, valgrind and cppcheck are clean.

Not done, and recorded as limitations: `kill`/`disown`, warning on exit with
stopped jobs, `jobs -l`, and a pty matrix for orphaned process groups or an
80×24 SSH session. Those are M8 polish, not blockers for the next milestone.

## M5 — Interactive Experience

Scope: raw-mode editor, cursor movement, history (file persistence with
permissions), completion (commands, paths), resize handling, prompt
configuration, `$'...'` quoting, `printf`, `read`.

Status: **done** (2026-11-19), with scope notes.

Delivered: `src/line/editor.c` (raw mode only while editing, arrows,
Home/End/Delete, Ctrl-A/E/B/F/K/U/W/L/C/D/P/N, history recall, tab completion),
`src/line/history.c` (XDG state file, 0600, bounded, duplicate suppression),
SIGWINCH redraw, `WONDERLAND_PROMPT`, `$'...'` quoting, and a `read` builtin.

Evidence: 75 unit cases (298 assertions, including history save/load and file
mode), 142 CLI checks, and a pty test that exercises history recall, Ctrl-A
insertion, Ctrl-U, tab completion, raw-mode-at-prompt and
canonical-mode-during-commands. The 14 differential cases still agree with
dash and bash; ASan, valgrind and cppcheck are clean with gcc and clang.

Deviations: no kill ring or undo, no UTF-8 cursor movement (bytes are columns),
completion inserts the common prefix and a space/slash but does not list
candidates, no Ctrl-R history search, and `printf` remains the external
utility (a builtin is M8 polish). The prompt override is verbatim, without
escape expansion.

Exit criteria as met: the editor survives prompt redraws and a resize signal,
terminal settings are restored for every foreground command and at exit, and
the pty test runs entirely without `stty sane`. A 10k-line history and
closed-terminal case are covered by the bounded file and the pty teardown,
not by a dedicated stress test.

## M6 — Narrative Foundation

Scope: content-pack loader per `docs/CONTENT_FORMAT.md`, persona records from
packs, event system with the enum in `docs/PERSONAS.md`, selection with
frequency/cooldowns/anti-repetition, `possess`, config keys, `--plain`
plumbing end to end, the remaining four wave-1 personas (12–40 lines each).

Status: **done** (2026-11-19), with the documented M6 subset (D-0102).

Delivered: `src/content/pack.c` (parser + provenance validation + atomic
rejection + directory scan + dedupe), `src/narrative/narrative.c` (default
persona, effective success/fail mapping, frequency, per-kind cooldowns, global
cooldown, 12/600 s budget, 25-line no-repeat ring, deterministic cursors,
placeholder rendering), `possess`/`-l`, pack discovery through
`WONDERLAND_PACKS` and the XDG data directory, `make install` of core packs,
and five core personas: Alice 12 lines, Grimm 14, Nietzsche 13, Kafka 13,
Cioran 13 — all original.

Evidence: 93 unit cases (350 assertions) including loader provenance rules,
here-documents, duplicate ids, directory order, a 400-input fuzz pass, engine
cadence/cooldown/budget/possess/placeholder tests; 143 CLI checks; 14
differential cases; pty sessions for plain editing, narrative speech +
`possess` listing/switching, and `--plain` silence. ASan, valgrind and
cppcheck clean with gcc and clang. `make install` verified from `/tmp` using
the installed packs.

Deviations: one persona per pack; only the eight emitted events are accepted
(`pre_exec`, `idle`, `rest`, `respawn`, `cycle` arrive with M7); job events are
selectable but not emitted yet; `content_license = unspecified` pending OQ-1.
The 30-minute cadence and blind-attribution gate are human checks, still to be
run; the engine and content are in place for them.

## M7 — Wonderland Identity

Scope: Alice's Library cycle counter with atomic, versioned, checksummed
state; `rest`/`respawn` scoped exactly as `docs/PERSONAS.md` §8; death screen;
Black Rabbit presentation; mirror mode restricted to owned presentation;
`go-to-alice` sharing `exit` logic with optional confirmation.

Status: **done** (2026-11-19), with the documented opt-ins and limitations in
D-0104…D-0108.

Delivered: `src/core/state.c` (versioned hex state file with FNV-1a checksum,
0600 temp file, `fsync` plus `rename`, directory creation, corruption as a
fresh cycle), cycle increment and persona persistence at interactive startup,
`rest`, `respawn` (cwd, PATH/HOME/PWD/OLDPWD/SHELL, persona; explicit "not
process resurrection" message), `go-to-alice` through the same exit path as
`exit`, interactive confirmation disabled by `WONDERLAND_CONFIRM_EXIT=0`,
opt-in death screen (`WONDERLAND_DEATH_SCREEN=1`), mirror prompt fragment
(`WONDERLAND_MIRROR=1`), and Black Rabbit job_start/job_finish emission from
job-table counters.

Post-M7 presentation: the stock prompt is now Starship-compatible, with a
left duration/jobs/user/character prompt and a right-aligned directory, git
branch and clock (D-0111).

Evidence: state round-trip, permissions, missing/corrupt/truncated/wrong-version
files, and `clear_rest` in unit tests; pty coverage of `rest`/`respawn`,
`pwd` restoration, persona persistence across sessions, cycle 1→2, corrupt
state recovery with one note, mirror prompt, death screen not changing `$?`,
and deterministic job_start/job_finish narration via a test pack. Plain and
non-interactive sessions never read or write the state file.

Exit criteria: corrupt state file ⇒ fresh start with one note (pty test);
`rest`/`respawn` round-trip (unit + pty); themed mechanics do not change any
exit status (pty checks `$?` after a death-screen failure; CLI checks plain
statuses).

## M8 — Compatibility and Polish

Scope: selected Bash extensions (`${var//a/b}`, `<<<`, `local`, `pipefail`,
`[[ ]]`, arrays last), broader test suite, memory/resource audit (ASan, leak
checks), documentation pass, installation instructions that keep the shell
*beside* bash, example packs. `make install` exists now and installs to
`~/.local/bin` by default (D-0095); packaging and a `/usr/local` decision
remain M8 work.

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
`src/jobs/signal.c`, `src/line/read.c`, `src/exec/{path,exec}.c`,
`src/core/{shell,opts}.c`, `src/builtins/builtins.c` (`cd`, `exit`), the loop
and `-c`/stdin handling in `src/main.c`, and tests at three levels (unit, CLI,
pty). The `pty` test drives a real terminal, not a mock. (`m1_split.c` existed
here and was deleted in M2.)

## Recommended scope for the Milestone 8 prompt

> Implement M8 per `docs/ROADMAP.md` and `docs/COMPATIBILITY.md`: add
> `local`, `printf`, `kill`/`disown`, `${var//a/b}`, `<<<`, `pipefail` and
> the safest parts of `[[ ]]` (arrays last); grow each core persona toward
> its line target (Alice 60, majors 40) with original writing and the blind
> attribution gate; add `tests/cases/` fixtures for the new syntax and keep
> `make diff` green; polish `make install` (man page, completion file) while
> keeping the shell out of `/etc/shells`; update README, COMPATIBILITY and
> DECISIONS with what is actually supported.

After M8: continue the dogfooding gate and the remaining cast (20–30
personas), then consider `/etc/shells`.