# Wonderland — Risks

Status: design. Ordered by how likely they are to hurt, with the mitigation
that is supposed to prevent the damage and the signal that tells us it is
happening.

## 1. Risk register

### R1. The shell never reaches "daily drivable"
**Likelihood: high. Impact: fatal to the premise.**
A full POSIX shell is years of edge cases, and every milestone so far is
"almost working". The narrative layer is more fun, so attention drifts.
*Mitigations:* the milestone order is fixed and execution-first; the narrative
slice is capped at 12 lines and one hook; "definition of done" for M1–M3 is
behavioural, not structural; the dogfooding gate at M5 forces real use.
*Early warning:* M3 slips past its exit criteria while persona content grows.

### R2. Narrative creeps into execution
**Likelihood: medium. Impact: violates the non-negotiables.**
Someone adds "just one hook" that reads `$?`, or a persona prints directly to
stdout for convenience, or `exec` calls a persona to decorate a failure.
*Mitigations:* `docs/ARCHITECTURE.md` §3.1 — separate archive restricted
include path, header hygiene, `-Wl,--no-undefined` link probe,
`scripts/check-isolation.sh`; event structs are `const`; narrative returns text
and never performs I/O. The isolation check runs in `make test`, so a violation
fails CI, not review.
*Early warning:* a narrative file that includes `core/` or `exec/`; a new
`wl_narrative_*` symbol referenced inside `exec/`.

### R3. Quoting and field splitting are subtly wrong
**Likelihood: high. Impact: scripts break silently, the worst kind of bug.**
Quoted/unquoted boundaries, `$@`, IFS, empty fields, glob-vs-literal are the
classic shell correctness swamp.
*Mitigations:* the grammar keeps quote information per word part from the
start (`docs/GRAMMAR.md` §2.3); the §10 matrix is written before the code; the
differential harness compared against bash and dash; no "string splitting
shortcut" is allowed to survive M2.
*Early warning:* tests that only check "roughly the right output"; any code
that re-tokenises a string in the expander.

### R4. Job control and terminal ownership races
**Likelihood: high. Impact: unusable terminal, orphaned processes, data loss
in editors.**
`tcsetpgrp`/`SIGTTOU`/`SIGTTIN`/`SIGCHLD` interactions are racy by nature, and
a mistake leaves the user in raw mode with no echo.
*Mitigations:* one reaper, self-pipe signal handling, process-group tests
under a pty with `ps` assertions, restore-on-every-exit-path with a guard flag,
and the "never write while a foreground job owns the tty" rule.
*Early warning:* `stty sane` appearing in bug reports; tests that need
sleep-based synchronisation.

### R5. Memory ownership bugs (use-after-free, leaks, double free)
**Likelihood: medium-high. Impact: crashes, and crashes in a shell are scary.**
Arena-per-line plus manual string handling is simple but unforgiving;
expansion allocates heavily.
*Mitigations:* documented `_new`/`_borrow`/`_dup` conventions, one allocator
choke point, ASan/UBSan in `make asan`, fuzz corpus for lexer/parser/loader,
leak checks on the long-running loop (`valgrind` is **not installed** here —
see R12).
*Early warning:* `const`-incorrect APIs; ownership comments in headers diverging
from call sites.

### R6. Signal-handler work that is not async-signal-safe
**Likelihood: medium. Impact: deadlocks and corruption, hard to reproduce.**
It is tempting to `waitpid` inside `SIGCHLD` or `printf` inside `SIGINT`.
*Mitigation:* handlers write one byte to a self-pipe and return; the rule is in
`AGENTS.md` and checked in review. *Early warning:* any handler longer than
three lines.

### R7. Copyright and attribution failure in content
**Likelihood: medium. Impact: legal and reputational; also breaks trust in the
"verified" promise.**
Black Souls dialogue and modern translations are copyrighted; users will share
packs; a fabricated "Nietzsche quote" is embarrassingly easy.
*Mitigations:* the pack format requires provenance; `canonical` demands
`status = verified` or the pack is rejected; example packs are original-only;
the repository never ships canonical or quoted material; `content_license` is
explicit.
*Early warning:* a pack with `category = quotation` and no `translation`; a
line that reads too well to be a translation.

### R8. Parser stack exhaustion / denial of service
**Likelihood: medium. Impact: crash on adversarial input; a shell must not
crash because of a pasted line.**
Recursive descent plus nested `$(...)`, `((((...`, and giant here-docs.
*Mitigations:* explicit depth and size limits in `docs/GRAMMAR.md` §7, tests at
the limits, fuzzing under ASan; limits produce diagnostics, never aborts.
*Early warning:* a stack-overflow backtrace; recursion without a depth counter.

### R9. Performance and prompt latency
**Likelihood: low-medium. Impact: the shell feels bad, and "feels bad" is
fatal for daily use.**
Narrative selection is cheap by construction, but a naive glob on a huge
directory or a synchronous stat-heavy prompt can cost hundreds of ms.
*Mitigations:* bounded selection (in-memory, O(candidates)); prompt does no
`stat` beyond `getcwd`; pathological globs get an explicit budget and a test;
no narrative work at all in plain mode.
*Early warning:* prompt timing above 5 ms on a warm cache.

### R10. Scope explosion from theme mechanics
**Likelihood: medium. Impact: months lost, shell still unfinished.**
Mirror mode, respawn, cycle events, rare character behaviours all invite
elaboration.
*Mitigations:* amendment 9 caps them at observational or thin builtins;
mechanics are in M7, after the shell works; each mechanic needs a test that
proves it does not change status or stdout.
*Early warning:* a themed feature with its own source file before M7.

### R11. Corrupt persistent state
**Likelihood: medium. Impact: startup failure or lost cycle counter — annoying,
not fatal, unless it crashes.**
*Mitigations:* versioned header, checksum, atomic temp-file + `rename()`,
"corrupt ⇒ ignore with one note", tests with truncated and bit-flipped files.
*Early warning:* any `fscanf`-style parsing of the state file.

### R12. Toolchain gaps in the audit environment
**Likelihood: certain (measured). Impact: weaker verification than claimed.**
`valgrind`, `dash`, and `cppcheck` are not installed; `sh` is bash, so the
"conservative POSIX" reference is missing.
*Mitigations:* ASan/UBSan replace valgrind for the C parts; the differential
runner skips dash with a visible warning and supports `WL_REQUIRE_DASH=1`;
`docs/COMPATIBILITY.md` §2 states the gap. Do not pretend `sh` = dash.
*Early warning:* a compatibility claim whose test ran only under bash.

### R13. Terminal/app interference from presentation
**Likelihood: medium. Impact: corrupts `vim`, `less`, `htop` display; users
lose work.**
Any narrative write while a full-screen program runs, or a prompt that emits
colours into non-tty stdout, is destructive.
*Mitigations:* gate on `isatty(stdin) && isatty(stderr)`; no write while a
foreground job runs; never write to stdout; no DEC private modes outside
owned presentation; test with `vim`/`less` under pty.
*Early warning:* screenshots of corrupted TUI output.

### R14. Bash-extension pressure distorts the grammar
**Likelihood: medium. Impact: parser complexity, POSIX regressions.**
Arrays change the word model; `[[ ]]` changes reserved-word handling.
*Mitigations:* extensions are last (M8), opt-in where possible, and each one
needs a "does this break POSIX?" review; the grammar doc lists them as
explicit non-goals today.
*Early warning:* `N_ARRAY` appearing in `ast.h` before M8.

### R15. Contributor (and author) context loss
**Likelihood: medium. Impact: inconsistent decisions, re-litigated arguments.**
This is a long project with one primary author and long gaps possible.
*Mitigations:* everything decided is written in `docs/DECISIONS.md` with a
number and a date; `AGENTS.md` is the onboarding contract; milestone handoffs
record limitations explicitly.
*Early warning:* a change that contradicts a decision with no new decision.

### R16. Editing the wrong repository / losing work
**Likelihood: low. Impact: high when it happens.**
This bootstrap ran in an empty directory, but the project will be cloned in
several places.
*Mitigations:* validate `pwd` and `git status` before bulk changes; never
`git add -A` blindly in a directory with unknown files; commits are scoped.
*Early warning:* unfamiliar files in `git status` before a commit.

## 2. Risks accepted, not mitigated

* **Linux-only.** Non-portable interfaces will creep in; portability is not a
  goal and will not be claimed.
* **Single maintainer.** Bus factor is 1 until others contribute.
* **Narrative quality is subjective.** The blind-attribution gate is a
  heuristic, not a proof.
* **No conformance suite yet.** Until one is integrated, "POSIX" means "matches
  the cases we wrote", which is weaker than it sounds.

## 3. Tripwires that stop work

If any of these happen, stop feature work and fix the foundation:

1. A test needs `sleep` to pass reliably.
2. A command produces different stdout with the narrative library linked.
3. Terminal state is wrong after any exit path.
4. A malformed pack or a 10 kB line crashes the shell.
5. ASan reports anything, including a leak in the loop.
6. `make test` is red on `main` for more than a day.
