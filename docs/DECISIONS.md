# Wonderland — Decision Log

Append-only. Newest last. Every entry has a number, a date, a status and the
reason. If a change contradicts an entry, add a new entry that supersedes it;
never edit history away.

Statuses:

* **accepted** — settled, implement it.
* **proposed** — the current plan, not yet validated by code or use; may change
  with a new entry.
* **open** — needs a decision, listed again in §3.

Why this file exists: the project will be developed in long, interrupted
milestones. A decision that lives only in a chat log is a decision that will be
re-litigated.

---

## §1. Provenance and naming

### D-0001 — The brief is the source of truth; amendments override
**2026-11-18 · accepted**

`docs/original-brief.md` holds the full brief verbatim, as supplied on
2026-11-18 after the bootstrap prompt's placeholder. Where the nine amendments
in the bootstrap task differ from the brief, the amendments win. Where the
amendments are silent, the brief wins. Where neither speaks, these entries
decide, and the entry names the gap.

### D-0002 — The project is Wonderland; ABGRUND is dead
**2026-11-18 · accepted**

Former name: ABGRUND. The word appears only in historical notes like this one.
No code, variable, env var, file or user-facing string uses `ABGRUND`.
`ABGRUND_PLAIN` is not read, not aliased, not mentioned in help text.

---

## §2. The nine amendments, recorded

### D-0003 — Amendment 1: plain mode is `--plain` and `WONDERLAND_PLAIN=1`
**2026-11-18 · accepted**

Both are supported. The brief's §6 note "ABGRUND_PLAIN=1 should be replaced
with WONDERLAND_PLAIN=1" is honoured; the banner in the prompt is treated as
stale. Plain mode disables all narrative output and decorative behaviour.
Precedence and truthiness are specified in D-0033.

### D-0004 — Amendment 2: narrative only when interactive, never on stdout
**2026-11-18 · accepted**

Narrative output requires: `isatty(STDIN_FILENO) && isatty(STDERR_FILENO)`,
no `--plain`, no truthy `WONDERLAND_PLAIN`, and narrative enabled in config.
Destination: stderr by default, `/dev/tty` when configured (see OQ-2).
`wonderland -c ...` and `wonderland FILE` are always plain, regardless of
tty state. Structurally enforced by having narrative return text instead of
performing I/O (D-0015).

### D-0005 — Amendment 3: personas are read-only; builtins own state
**2026-11-18 · accepted**

Personas consume immutable `struct wl_event` values. State changes belong to
`possess`, `rest`, `go-to-alice`, `exit` and the shell core. Isolation is
structural: `libwl_narrative.a` with a restricted include path, header hygiene,
a `-Wl,--no-undefined` link probe, and `scripts/check-isolation.sh`. See
`docs/ARCHITECTURE.md` §3.1.

### D-0006 — Amendment 4: not a system shell yet
**2026-11-18 · accepted**

No `/etc/shells` entry, no `chsh`, no login-shell installation until the
dogfooding criteria in `docs/COMPATIBILITY.md` §9 are met: four weeks of daily
use beside bash, green differential tests for the POSIX surface, no known
data-loss bug, exercised job control, documented recovery path.

### D-0007 — Amendment 5: content provenance is mandatory
**2026-11-18 · accepted**

Each line carries `status` ∈ {verified, unverified, original} and `category` ∈
{canonical, quotation, original}, plus author/work/source/translation as
applicable. The loader **rejects** a pack that claims `canonical` without
`status = verified` and full provenance. This repository commits no Black
Souls dialogue and no third-party quotations; example packs contain only
clearly marked original lines. Format: `docs/CONTENT_FORMAT.md`.

### D-0008 — Amendment 6: five voices first, then scale
**2026-11-18 · accepted**

Wave 1: Alice, Grimm, Nietzsche, Kafka, Cioran — chosen for maximal mutual
distinctness, not for thematic completeness. Scale toward 20–30 after the
voices are proven by the blind-attribution test. Long-term targets: main
characters 200–300+ lines, major philosophers 100–200+, secondary 50–100+.
Quality over filler; a persona with 20 good lines beats one with 300 generated
ones. `docs/PERSONAS.md`.

### D-0009 — Amendment 7: vertical narrative slice after M3
**2026-11-18 · accepted**

After execution (M3) and before job control: one persona (Alice), ~12 original
lines, the `post_exec` hook, the plain-mode gate, and the isolation
machinery — a static table in the library, not the pack loader. Rationale:
prove the contract while it is cheap to change. `docs/ROADMAP.md` M3.5.

### D-0010 — Amendment 8: POSIX first, bash second, claims last
**2026-11-18 · accepted**

POSIX sh semantics first; selected bash extensions after M8. `#!/bin/bash`
scripts always run under real bash through the kernel; Wonderland never
intercepts shebangs and never fakes `BASH_VERSION`. Compatibility claims are
per-feature and require a passing test named in `docs/COMPATIBILITY.md`.

### D-0011 — Amendment 9: theme mechanics are observational or thin builtins
**2026-11-18 · accepted**

Alice's Library (cycle counter), death screen on non-zero status, `rest`
checkpoint, Black Rabbit (jobs), mirror mode (owned presentation only),
`go-to-alice` (shares `exit` logic). None may change an exit status, none may
run in plain mode, none may write to stdout, and no mechanic may be
implemented before M7 except the one vertical slice.

---

## §3. Technical foundation

### D-0012 — C17, libc + POSIX, Linux only
**2026-11-18 · accepted**

`-std=c17 -D_POSIX_C_SOURCE=200809L`, no third-party libraries, no runtime
dependency, no database, no service. `_DEFAULT_SOURCE` only where POSIX does
not expose a needed symbol, with a comment. Byte-oriented internals; no
locale-dependent classification in the lexer.

### D-0013 — Fifteen subsystems map to thirteen source directories + `main.c`
**2026-11-18 · accepted**

Mapping in `docs/ARCHITECTURE.md` §2. Three deliberate adjustments:

* History and completion live in `src/line/` — they are one state machine with
  the editor; a separate directory would duplicate the cursor/undo context.
* Configuration lives in `src/core/` — it is inert data with no lifecycle.
* `src/main.c` is added as the process entry point; the brief lists subsystems,
  not files.

No other deviations from the brief's directory list.

### D-0014 — GNU Make, static archives, separate object trees
**2026-11-18 · accepted**

Targets `all`, `test`, `asan`, `clean`, plus `check-isolation`. `libwl_shell.a`,
`libwl_narrative.a`, `wonderland`, `wl_tests`. ASan/UBSan builds in
`build/asan/` so they never contaminate the normal objects. No CMake, no
autotools, no recursive make.

### D-0015 — Narrative returns text; the shell performs I/O
**2026-11-18 · accepted**

`wl_narrative_react()` returns `struct wl_line { const char *text; size_t len; }`
borrowed from pack storage. This makes stdout purity structural rather than a
matter of discipline, makes plain mode one branch, and makes narrative output
testable without a terminal. Narrative performs no writes, no allocations
owned by the shell, and cannot block.

### D-0016 — Words are part lists; quoting is data, not re-parsing
**2026-11-18 · accepted**

Each word keeps `wl_word_part` records with a `quoted` flag from the lexer
(`docs/GRAMMAR.md` §2.3). Field splitting and globbing consult the flag;
nothing re-reads or re-splits source text. `""` produces a zero-length quoted
part so empty arguments survive.

### D-0017 — Hand-written recursive descent, fail-fast, byte spans
**2026-11-18 · accepted**

One syntax error per parse (matching `sh`), with a caret diagnostic. Spans
carry byte offsets *and* line/column, because snippet rendering and here-doc
slicing need offsets. This is a deliberate divergence from PercyLang's
`Position{Line, Column}` (see the reuse audit). Precedence climbing for binary
operators, as in PercyLang, is retained as a pattern.

### D-0018 — AST lives in a per-line arena
**2026-11-18 · accepted**

No per-node free, no refcounts, no GC. `wl_arena_reset()` after each command
line; session-lifetime data (variables, packs, history) uses a separate arena.
Node count is bounded by the input limits in `docs/GRAMMAR.md` §7.

### D-0019 — Ownership is explicit; no garbage collector
**2026-11-18 · accepted**

Conventions in `docs/ARCHITECTURE.md` §6 (`_new`, `_free`, `_borrow`, `_dup`,
`wl_arena_alloc`, `const T *` = never retained). PercyLang's Boehm-GC-based
runtime is explicitly *not* adopted; a shell needs predictable teardown, not
conservative collection.

### D-0020 — Signals: one byte to a self-pipe; one reaper
**2026-11-18 · accepted**

Handlers write a byte and return. All state changes (reaping, terminal
ownership, resize) happen in the main loop. `tcsetpgrp` is retried on `EINTR`.
The shell ignores `SIGTTOU`/`SIGTTIN`/`SIGTSTP` while it owns the terminal and
restores ownership after every foreground job.

### D-0021 — Content packs: constrained INI-like format with here-doc text
**2026-11-18 · proposed**

JSON rejected (escaping, unreadable diffs, nesting we do not need); TOML
rejected (value grammar we do not need); YAML rejected (too much spec).
Sections `[pack]`, `[persona id]`, `[line id]`; `key = value`; `text <<EOF`
for multi-line; `#` comments; no includes, no escapes. Detailed rules and
limits in `docs/CONTENT_FORMAT.md`. Revisit after the loader is written if the
format proves annoying in real authoring.

### D-0022 — Events: the brief's eleven plus five
**2026-11-18 · proposed**

Brief: `pre_exec`, `post_exec`, `on_success`, `on_fail`, `on_exit`, `on_idle`,
`on_job_start`, `on_job_finish`, `on_directory_change`, `on_rest`,
`on_respawn`. Added: `NOT_FOUND`, `PERMISSION`, `SIGNAL`, `SESSION_START`,
`CYCLE`. Reason: the brief's "situations" list (missing executable, permission
denied, background job completion, session start, previous-cycle events)
cannot be selected reliably from a generic `on_fail`. The added events are
specialisations of `post_exec`, carrying the same immutable fields.

### D-0023 — Commentary defaults: silence is normal
**2026-11-18 · proposed**

Base `post_exec` chance 12%, `fail` 45%, `dir_change` 8%, `exit` 60%,
`session_start` 100% once; per-kind cooldown 90 s (fail 45 s, rest/respawn
5 min); global 15 s; remember 25 line ids; suppress near-duplicates (token
Jaccard ≥ 0.8) over the last 3 lines; session budget 12 lines / 10 min;
idle threshold 20 s, off by default. These are opinions and are expected to
need tuning after M6; the constants live in one header so tuning is a
one-file change.

### D-0024 — Configuration: `key = value`, no includes
**2026-11-18 · proposed**

`$XDG_CONFIG_HOME/wonderland/config` (fallback `~/.config/wonderland/config`).
Precedence: defaults < file < environment < command line. Unknown keys warn
once and are ignored; they never abort startup. No includes in v1 (include
cycles and path traversal are not worth the convenience).

### D-0025 — Persistent state: versioned, checksummed, atomic, disposable
**2026-11-18 · proposed**

Location `$XDG_STATE_HOME/wonderland/state` (fallback
`~/.local/state/wonderland/state`). Header with magic, version, checksum;
write to a temp file in the same directory, `fsync`, `rename`. Any corruption
or version mismatch ⇒ ignore the file, start a fresh cycle, print one stderr
line in interactive mode. Never crash, never guess.

### D-0026 — Exit codes for the bootstrap binary
**2026-11-18 · accepted**

`0` success (`--version`), `2` usage error (unknown option, or `-c` without its
argument), `3` "recognised but unimplemented" (no operands, `-c '...'`, a
script path, or stdin). `-c`, script paths and stdin mark the session plain, as
amendment 2 requires, even though no command runs yet. Notices go to stderr;
stdout stays empty. This is temporary and is removed in M1, but it keeps the
skeleton honest and testable. The exact strings are asserted in
`tests/run.sh`, so a change to the message is a visible diff rather than a
surprise.

### D-0027 — Tests: tiny C harness plus black-box shell runner
**2026-11-18 · accepted**

No external test framework. `tests/unit/test.h` provides `WL_ASSERT*` macros
and a registration table; `tests/unit/test_main.c` runs suites; failures print
`file:line: expression` and the runner exits 1. `tests/run.sh` drives the built
binary as a black box (args, env, exit codes, stdout/stderr separately).
Interactive behaviour will use `forkpty` in C, not `expect`. Rationale: the
project's dependency policy applies to tests too, and a 150-line harness is
easier to trust than a framework's magic.

### D-0028 — Missing tools: no valgrind, no dash, no cppcheck
**2026-11-18 · accepted**

Measured in this environment: valgrind, dash, cppcheck absent; `sh` is bash
5.3.20. Consequences: ASan/UBSan (`make asan`) is the memory tool of record;
differential tests skip dash with a visible warning and fail when
`WL_REQUIRE_DASH=1`; `sh` is never described as dash. Recorded as risk R12.

### D-0029 — GitHub: `etokiyra/wonderland`, public, branch `main`
**2026-11-18 · accepted**

Created during bootstrap because `gh auth status` showed an authenticated
`etokiyra` session with `repo` scope. No CI is configured yet (M8).

### D-0030 — No LICENSE file yet
**2026-11-18 · open**

Deliberately omitted. The choice interacts with content licensing (packs may
carry quotations) and with PercyLang reuse (GPL-3.0-or-later root, MIT for the
C runtime — see the audit). See OQ-1.

### D-0031 — Empty directories carry `.gitkeep`
**2026-11-18 · accepted**

Git does not track empty directories, and the structure is part of the design
contract. `.gitkeep` files are placeholders and carry no meaning.

### D-0032 — Narrative library ships one placeholder translation unit
**2026-11-18 · accepted**

`src/narrative/narrative.c` defines `wl_narrative_layer_name()` only. Reason:
an empty archive cannot be built or link-probed, and the isolation machinery
must be exercised from M0 rather than asserted in prose. It contains no event
handling and no content; M3.5 replaces it with the real slice.

### D-0033 — Plain-mode precedence and truthiness
**2026-11-18 · proposed**

Order (strongest first): `--plain` > `WONDERLAND_PLAIN` > non-interactive tty
state > config. `WONDERLAND_PLAIN` counts as true when set to any value other
than `0`; unset or `0` is false. A future runtime toggle may only disable, never
re-enable, narrative in a session that started plain. See OQ-3.

### D-0034 — Byte columns in diagnostics
**2026-11-18 · accepted**

`file:line:column` uses byte columns (1-based), not character columns. Same
reasoning as PercyLang's documented byte/rune divergence: shells are
byte-oriented and the lexer indexes bytes. Documented so caret rendering is not
surprising in UTF-8 text.

### D-0035 — `-Werror` is not on by default
**2026-11-18 · accepted**

Default flags: `-std=c17 -Wall -Wextra -Wpedantic`. Warnings must be zero, but
a stricter compiler (this environment has GCC 16 and Clang 23) should not break
someone's build over a new diagnostic. A separate `make strict` may add
`-Werror`; reviewers treat any new warning as a failure regardless.

### D-0036 — `make test` includes the isolation checks
**2026-11-18 · accepted**

`test` runs unit tests, `tests/run.sh`, `scripts/check-isolation.sh`, and the
narrative link probe. If isolation is broken, the default test target fails —
the rule is not a documentation-only aspiration.

### D-0037 — The M1 line reader is disposable and marked as such
**2026-11-18 · accepted**

M1 splits lines on whitespace so that execution can be tested before the
lexer exists. It is marked `M1-ONLY`, has no quoting or expansion, and is
deleted in M2. This is the one allowed temporary shortcut; it exists to keep
the milestone order honest, not to become the architecture.

### D-0038 — `rest` stores a narrow, documented state set
**2026-11-18 · proposed**

cwd, whitelisted variables (`PATH`, `HOME`, `PWD`, `OLDPWD`, `SHELL`, plus a
user-configured list), active persona id, narrative state (cycle, recent ids,
cooldowns), format version. Never processes, fds, jobs, or terminal state.
`respawn` reports anything it could not restore instead of pretending.

### D-0039 — `go-to-alice` and `exit` share one code path
**2026-11-18 · proposed**

Both call an internal `wl_builtin_exit_impl()`. Confirmation is a property of
the exit path, not of the alias, and is disabled by
`WONDERLAND_CONFIRM_EXIT=0` or config `confirm_exit = false`. Non-interactive
`exit` is never confirmed. No shell alias is used (the brief forbids it).

### D-0040 — The repository ships original example content only
**2026-11-18 · accepted**

`packs/examples/alice.pack` contains six `status = original` lines written for
this project. `content_license = unspecified` until OQ-1 is decided; until
then the pack is part of this repository only and is not offered for
redistribution elsewhere.

### D-0041 — No speculative abstractions
**2026-11-18 · accepted**

Recorded non-abstractions are listed in `docs/ARCHITECTURE.md` §13: no AST
visitor framework, no polymorphic builtins, no event bus, no threading, no
plugin ABI before packs are proven. A new abstraction needs a concrete second
user in the same milestone.

### D-0042 — Documentation is normative
**2026-11-18 · accepted**

Where docs and code disagree, that is a bug in one of them, named in the
review. `docs/COMPATIBILITY.md` is the only source of compatibility claims;
`docs/GRAMMAR.md` is the only definition of accepted syntax;
`docs/CONTENT_FORMAT.md` is the only definition of pack validity.

---

## §4. Open questions

These require a human decision. They are ordered by how soon they block work.

**OQ-1 — License.** No `LICENSE` file. Options: MIT (matches PercyLang's C
runtime, maximally reusable), Apache-2.0 (patent grant, still permissive),
GPL-3.0-or-later (matches PercyLang root; forces copyleft on anything copied
from it). Interaction: if any GPL code is ever copied from PercyLang, GPL is
forced. Current plan is to copy no code, which keeps all options open. Needs a
decision before M8 installation docs and before any content redistribution.
Blocks: D-0030, D-0040, content_license values.

**OQ-2 — Default narrative destination: stderr or `/dev/tty`.** stderr is
simpler and composes with `2>log`; `/dev/tty` keeps the world visible when
stderr is redirected. Proposal: stderr by default, `/dev/tty` opt-in. Affects
D-0004 and the interactive tests.

**OQ-3 — `WONDERLAND_PLAIN` accepted values.** Proposal: `0` false, anything
else true. Alternative: strict `1` only, everything else a warning. Strictness
helps catch typos (`WONDERLAND_PLAIN=yes` silently doing nothing is a trap).
Affects D-0033.

**OQ-4 — Persona ids globally unique vs pack-qualified.** Global uniqueness is
simpler now; qualification matters once users share packs. Affects
`docs/PERSONAS.md` §12 and the pack id rules.

**OQ-5 — Idle timer owner.** Line editor or main loop? The editor knows about
keystrokes; the loop knows about jobs and prompts. Proposal: loop, with the
editor reporting last-input time.

**OQ-6 — State file format.** Custom text (easy to inspect) vs a small binary
struct (atomic, versioned). Proposal: text with a checksum line; it is tiny and
debuggable.

**OQ-7 — `make install` / install prefix.** Deferred to M8. Options:
`/usr/local` via `PREFIX`, or `~/.local`. No decision needed yet.

**OQ-8 — Pack validation at build time.** `make check-packs` on
`packs/examples/` is cheap and catches typos; it also means the build depends
on the pack parser existing (M6). Proposal: add at M6, not before.

**OQ-9 — Content license for example packs.** Separate from the code license;
CC0 is the obvious candidate for purely original lines, but it is a commitment
and should be decided with OQ-1.

**OQ-10 — Whether `set -e` follows POSIX text or dash's implementation where
they differ.** Must be decided with evidence from the differential suite; do
not guess now. Affects `docs/COMPATIBILITY.md` §6.

---

## §5. Superseded decisions

* **D-0028** (missing `dash`, `valgrind`, `cppcheck`) is superseded by
  **D-0048**: the tools are now installed. The policy it stated (`sh` is not
  dash; ASan is the memory tool of record) still holds where it is not about
  availability.

## §6. Verification notes

### D-0043 — The three isolation mechanisms were tested by breaking them
**2026-11-18 · accepted**

Claims about enforcement are only worth the failure they produce. Three
deliberate violations were committed to `src/narrative/` temporarily and then
removed:

1. `#include "core/opts.h"` in a narrative TU ⇒ **compile error**
   (`core/opts.h: No such file or directory`), because the narrative compile
   line only adds `-Iinclude/wonderland/narrative`.
2. `fork()` with no forbidden include ⇒ **`scripts/check-isolation.sh`
   fails** with `forbidden call in .../poison.c`.
3. An `extern int wl_shell_interior_state(void)` reference with no definition
   ⇒ **`make check-isolation` fails at link time** with
   ``undefined reference to `wl_shell_interior_state'``, proving the
   `--whole-archive --no-undefined` probe pulls in every narrative object.

After removal, `make test` was green again (15 unit cases, 24 CLI checks). If a
future change weakens any of these three, that test is the one to re-run by
hand before trusting the docs.

---

## §7. Milestone 1 implementation decisions

### D-0044 — M1 includes foreground process-group handoff
**2026-11-19 · accepted**

M1 gives each external command its own process group, hands it the terminal
with `tcsetpgrp`, waits, and takes the terminal back. This is a deliberate
subset of M4, included early for one reason: without it, Ctrl-C during a
command kills the shell as well as the child, and the shell is not usable
enough to dogfood. What M1 still lacks, and M4 adds: a job table, `jobs`/`fg`/
`bg`, `WUNTRACED`/`SIGCONT` handling, background jobs, the `SIGCHLD`
self-pipe, `wait`, and traps on `EXIT`/`INT`/`TERM`.

### D-0045 — SIGINT is a flag, not yet a self-pipe
**2026-11-19 · accepted**

The interactive handler sets a `volatile sig_atomic_t` and returns; `read(2)`
returns `EINTR` because the handler is installed without `SA_RESTART`. There
is no `poll()` loop yet, so a self-pipe would have nothing to wake. This
satisfies the safety rule ("one async-signal-safe action") without inventing
plumbing for M4 to replace. The line reader uses `read(2)` rather than stdio
because stdio may retry after `EINTR` and swallow the interrupt, and because
canonical-mode `read(2)` returns at most one line, so nothing is lost.

`AGENTS.md` §2 rule 6 is amended by this entry: a handler may set a
`sig_atomic_t` flag *or* write one byte to the self-pipe, never both, never
anything else.

### D-0046 — The prompt goes to stderr, in one tested shape
**2026-11-19 · accepted**

The prompt is shell presentation, not narrative and not command output, so it
is written to stderr and flushed before each read. It stays visible in plain
mode (plain mode suppresses the world, not the shell). Shape:

```
wonderland:<path>[ [status]]$ 
```

`<path>` abbreviates `$HOME` to `~`; a path longer than 48 bytes is reduced to
`../<leaf>` so the prompt stays usable in narrow terminals; no colour yet. The
format is a pure function (`wl_prompt_format`) with exact-byte tests, because
prompt bugs are otherwise found by eye.

### D-0047 — M1 execution contract and limits
**2026-11-19 · accepted**

* The M1 splitter is whitespace-only and marked `M1-ONLY`; it is deleted in
  M2 (D-0037).
* Limits: 4095-byte command lines and 255 arguments, refused with status 2 and
  a diagnostic, never truncated.
* `-c STRING` executes each newline-separated line in order. `;` is *not*
  special yet; it is an ordinary argument. This is stated in the usage text by
  omission and in the README as a limitation.
* An empty or whitespace-only line executes nothing and leaves `$?` unchanged,
  matching shell behaviour.
* `wonderland SCRIPT` is still refused with status 3; `-` reads stdin
  non-interactively. Script files arrive with the parser in M3.
* `SIGINT`-interrupted reads return to a fresh prompt without executing the
  partial line.

### D-0048 — Developer tooling is available; `make memcheck` and `make lint`
**2026-11-19 · accepted**

Installed in this environment: `dash` 0.5.13.5, `valgrind` 3.25.1, `cppcheck`
2.22.0. `make memcheck` runs the unit suite under valgrind
(`--error-exitcode=1`, definite leaks fail); `make lint` runs cppcheck with
warnings/performance/portability enabled and `--error-exitcode=1`. Both are
developer tools, not part of `make test`: `make test` must keep working on a
machine that has only a compiler and make.

### D-0049 — `exit` and `cd` edge semantics
**2026-11-19 · accepted**

* `exit N`: N must be a decimal integer in 0..255. Anything else prints
  `numeric argument required`, sets status 2 and stops the shell (POSIX-style),
  because continuing after a bad exit request is worse than stopping.
* `exit A B`: too many arguments; status 1 and the shell keeps running. The
  request is ambiguous, so it is refused rather than guessed.
* `exit` with no argument uses `$?` and stops with it.
* `cd` with no argument uses `$HOME`; `cd -` uses `$OLDPWD` and prints the new
  directory to stdout (POSIX behaviour, and the only builtin that writes to
  stdout). `cd` updates `PWD`/`OLDPWD`.
* Neither builtin has narrative, confirmation or aliases yet; those belong to
  M6/M7 and change status behaviour not at all.

### D-0050 — No `ENOEXEC` fallback to `sh`
**2026-11-19 · accepted (temporary)**

A file without a `#!` line, executed as a command, fails with
"Exec format error" and status 126. POSIX shells fall back to running it with
`sh`. Wonderland does not yet, because that fallback is a compatibility
feature that deserves its own test (M8) and must not be smuggled in as an
error-path accident. Recorded as a known limitation in the README. Scripts
with a shebang, including `#!/bin/bash`, already run under the kernel's chosen
interpreter.

### D-0052 — Spans are byte offsets; line/column is computed on demand
**2026-11-19 · accepted**

`struct wl_span` is `{ start, end }` in bytes. Line and column are derived by
`wl_diag_locate()` only when a diagnostic is printed. This supersedes the
sketch in `docs/GRAMMAR.md` §4, which carried `line`/`col` inside the span:
keeping them accurate through recursive sub-parses would need a line map per
source, and the only consumer is the error path. Columns remain byte-based
(D-0034).

### D-0053 — The lexer builds word parts; the parser resolves substitutions
**2026-11-19 · accepted**

Quoting is scanned exactly once, by the lexer, into `wl_word_part` records;
there is no second scanner that could disagree with the first. The lexer has
no parser dependency: `$(...)` and backticks record their inner source, and
the parser resolves those sources into sub-programs in a post-pass over the
finished tree (`wl_p_resolve_node`). Command-substitution nesting is bounded
by the same depth limit as grammar nesting (D-0058 notes the diagnostic
position caveat).

### D-0054 — Here-document bodies are stored raw, with an expansion flag
**2026-11-19 · accepted**

Bodies are read lazily when the newline that ends the command line is
*consumed*, before the next token is scanned; that ordering is what keeps body
text from being lexed as commands. The body is stored as bytes plus
`expand` (delimiter unquoted) and `strip_tabs` (`<<-`); it is **not** split
into word parts at parse time. M3 expands it as a single word when the
redirection is applied. This is simpler than the part-list sketch in
`docs/GRAMMAR.md` §5 and avoids freezing an expansion representation before
the expander exists.

### D-0055 — `;;` is a token; Bash case terminators are not accepted
**2026-11-19 · accepted**

The lexer emits `WL_TOK_DSEMI` for `;;` so a `case` body can stop at an item
boundary. `;&` and `;;&` are not tokenised; they parse as `;` followed by an
error, with a diagnostic. Case patterns are ordinary words, so `a|b)` and
`*)` need no special lexing. This keeps the case grammar free of Bash
fallthrough semantics that do not exist yet.

### D-0056 — Input units, continuation, and the syntax-error status
**2026-11-19 · accepted**

* `-c` and stdin are parsed as **one** source unit. `-c` is capped at 1 MiB
  (`WL_SOURCE_MAX`); stdin is capped at the same size while being read.
* Interactive input is one **logical line**, capped at 4095 bytes
  (`WL_LINE_MAX`). Two things trigger continuation: the parser reports
  `incomplete`, or the reader sees an odd number of trailing backslashes
  (a backslash-escaped newline, which the parser cannot see because it
  removes the escape before tokenizing). The continuation prompt is `> `.
  The whole logical line is re-parsed after each new physical line; the
  arena is reset between attempts, so the cost is bounded and garbage-free.
* A syntax error sets `$?` to 2, as `sh` does, and leaves the shell running.
  This replaces the M1 roadmap wording ("previous status intact"), which was
  aspirational and inconsistent with every real shell.
* EOF during a continuation reports `unexpected end of file` with status 2.

### D-0057 — M2 execution surface: syntax yes, process model no
**2026-11-19 · accepted**

`src/exec/m1_split.c` is deleted. Execution now consumes the AST and supports
simple commands with literal words, `;` lists, `&&`, `||` and `!`. Everything
else that the parser accepts is **refused with status 2 and one diagnostic**:
expansion, redirection, variable assignment, pipelines with more than one
command, background `&`, and compound commands. Refusal is per logical line;
the shell continues. This makes the parser shippable without pretending that
the process model exists, and keeps the M3 diff about execution rather than
about parsing.

### D-0058 — Names are validated; `${#x}` and `${x#p}` stay distinct
**2026-11-19 · accepted**

`for` names and function names must be `[A-Za-z_][A-Za-z0-9_]*`; anything else
is a syntax error. In `${...}`, a leading `#` before a name means length
(`${#x}`), while `#`/`##`/`%`/`%%` after the name mean trimming; a `:` before
`-/=/ ?/+` is recorded, and `:` before a trim operator is rejected. Bash-only
forms such as `${!x}` are syntax errors, not silently mis-parsed: an
unrecognised character before `}` reports `expected '}' to close ${...}`.

Diagnostics inside a command substitution currently carry offsets relative to
the substitution's own source, not the outer line, because the sub-parse sees
a copied string. The message is correct; the caret may point at the wrong
column. Fixing that needs a source-offset map and is deferred until the
interactive experience is polished (M5).

---

## §9. Milestone 3a implementation decisions

### D-0059 — Variables are a linked list with an explicit exported flag
**2026-11-19 · accepted**

`core/var.c` holds shell variables as a small singly linked list, seeded from
`environ` at startup. A variable has a value and an `exported` flag; the
environment handed to a child is rebuilt from the exported ones on every
command rather than kept in sync with `setenv`. A plain assignment never
clears an existing export; `export` sets it. This makes `export VAR`, `export
VAR=value`, and `VAR=value` behave predictably without a second source of
truth. Strings are malloc'd copies; the store is freed by
`wl_shell_destroy()`, which `main` and the tests call.

### D-0060 — Expansion carries two flags per byte, not just a string
**2026-11-19 · accepted**

The expander produces bytes with `quoted` and `split` flags. `quoted` comes
from quoting and suppresses globbing (quoted metacharacters are escaped for
`glob(3)`); `split` is true only for bytes produced by an unquoted expansion,
and only those bytes can delimit fields on `IFS`. Consequences:

* `a:b` with `IFS=:` is one field; `$x` containing `a:b` is two.
* `a"*"` does not glob; `a*` does.
* `""` survives as one empty argument; `$empty` produces no argument.
* An unmatched glob is left literal (POSIX), not an error.

Field splitting is an approximation for non-whitespace `IFS`: runs of
delimiters are collapsed and empty fields are dropped. That matches the
default IFS exactly; exotic IFS cases are a documented limitation (D-0068).

### D-0061 — `${x:-word}` quoting: overlay the operand's bytes
**2026-11-19 · accepted**

`${x:-a b}` splits into two fields when the whole expansion is unquoted,
while `${x:-"a b"}` does not. The expander therefore keeps the operand's own
per-byte quoting, overlays the outer quoting on top, and lets every resulting
byte split exactly when it is not quoted. This was checked against dash and
bash before the code was accepted; it is not an invented rule.

### D-0062 — Prefix assignments: environment for externals, temporary for builtins
**2026-11-19 · accepted**

`A=1 cmd` builds a child environment containing `A=1` and does not change the
shell's `A`. For builtins, the assignment is applied to a saved snapshot and
restored afterwards, so `A=1 cd /tmp` does not leave `A` behind. A standalone
`A=1` is a permanent shell assignment. Expansion of the command words happens
before prefix assignments are applied, as POSIX requires.

### D-0063 — Redirections are prepared in the parent, applied in the child
**2026-11-19 · accepted**

Targets, here-documents and here-strings are expanded and materialised in the
parent, so a failure is reported once, before any fork, with status 1.
External commands apply the prepared list left to right in the child; builtins
apply it in the parent with `fcntl(F_DUPFD)` save/restore, so `cd /tmp > log`
works without leaking a descriptor. Errors name the target and use the
`open(2)` errno. `>&`/`<&` accept only an fd number or `-`; anything else is
"bad file descriptor". Here-document bodies go through a `tmpfile()` fd marked
`FD_CLOEXEC`; unquoted delimiters expand the body (D-0067).

### D-0064 — Pipelines: one child per stage, first stage owns the terminal
**2026-11-19 · accepted**

Each stage is expanded and prepared in the parent, then forked; the first
child's process group receives the terminal, later children join it, and `$?`
is the last stage's status (128+N if it was signalled). Builtins in a pipeline
run in their child, so their state changes are discarded, as POSIX requires.
The parent closes every pipe end it does not need and waits for all stages.
Background jobs remain refused until M4; so does stopping/resuming a pipeline.

### D-0065 — Command substitution: fork, capture stdout, strip trailing newlines
**2026-11-19 · accepted**

`$(...)` is executed by a callback supplied by `exec` (the expander has no
dependency on execution). The child runs the sub-program with stdout on a pipe;
the parent reads to EOF and waits. All trailing newlines are removed, never
interior ones. The substitution's status does not overwrite `$?`, matching the
observable behaviour of dash and bash for `X=$(false); echo $?`. Captured
bytes are not size-limited beyond memory; a future limit belongs with the
source caps (D-0056).

### D-0066 — Arithmetic is integer-only and deliberately small
**2026-11-19 · accepted**

`$((...))` supports decimal/hex integers, `+ - * / %`, unary signs,
parentheses, whitespace, and variable names (with or without `$`). Division or
modulo by zero is an error (status 1), as is trailing garbage. Assignment
operators, comparisons, `**`, and floating point are not implemented; adding
them needs tests and a decision, not a drive-by extension. Unset variables
evaluate to 0.

### D-0067 — Here-document expansion has its own lexer mode
**2026-11-19 · accepted**

An unquoted delimiter marks the body for expansion, but the body is not a
normal word: whitespace does not separate, quotes are literal, and a backslash
escapes only `$`, backtick, backslash and newline (otherwise it stays). The
lexer gained a `heredoc` mode and `wl_lex_word_heredoc()` so the same
parameter/command/arithmetic code is reused without pretending the body is a
quoted shell word. Bodies are still stored raw (D-0054).

### D-0068 — Known M3a limitations
**2026-11-19 · accepted**

* `"$@"` with zero positional parameters yields one empty argument instead of
  zero; positional parameters themselves are not implemented yet.
* Non-whitespace `IFS` handling collapses repeated delimiters instead of
  preserving empty fields.
* `set -e/-u/-x`, `read`, `exec`, `trap`, `command`, `type`, `umask`, `wait`
  and `getopts` are still missing.
* A variable assigned in a pipeline stage is lost with the child, as intended,
  but `cd` in a pipeline is likewise local — documented, not a bug.
* Expansion happens in the parent; a command substitution that never finishes
  blocks the shell, exactly like any other foreground child.

---

## §10. Milestone 3b implementation decisions

### D-0069 — Positional parameters are frames; `"$@"` is special-cased
**2026-11-19 · accepted**

`core/shell.c` keeps a stack of positional frames: `$0` plus `$1..$n`. The base
frame is the script/`-c` invocation; each function call pushes a frame and pops
it on return. `shift` edits the current frame, and `set --` replaces it
(copying `$0` before the clear, which fixed a real use-after-free found by
ASan). `$#`, `$1`, `$*` and `${10}` read the frame.

`"$@"` as a *whole word* produces one field per parameter, including empty
ones. When it is adjacent to other text (`pre"$@"`), it joins like `"$*"`
instead; that is a documented limitation (D-0076), not an accident.

### D-0070 — Function bodies are re-parsed into a session-lifetime arena
**2026-11-19 · accepted**

The parse arena is reset after every command, so a function body cannot stay in
it. On definition the executor copies the definition's source text, re-parses
it into an arena owned by the function entry, and keeps the body node. This
keeps the AST allocator rules intact (no per-node free, no GC) at the cost of
one parse per definition. Redefinition replaces the body and frees the old
arena. Functions share the caller's variables (no `local` yet) and run in the
current shell, as POSIX requires.

### D-0071 — Groups run in place; subshells fork; compounds can be pipeline stages
**2026-11-19 · accepted**

`{ ...; }` executes in the current shell, with its redirections applied through
the same fd save/restore used for builtins. `( ... )` forks and exits with the
body's status, so variable and cwd changes cannot leak. A compound command in a
pipeline (`{ echo x; } | cat`) becomes a pipeline stage that runs its whole node
in the child; a function used as a stage runs in the child too. This is what
makes `if ...; fi | cmd` behave, and it stays within the existing process-group
model.

### D-0072 — break/continue/return unwind with counters
**2026-11-19 · accepted**

`break N` sets a pending count; each enclosing loop decrements and either
breaks (`break`) or continues/propagates (`continue`, where `N>1` breaks out one
level). Loops increment `loop_depth`, and a function call resets the loop depth
and the pending counts, because a loop in the caller is not a loop in the
function. `return` sets a flag plus status; the function call restores
`$?` from it. Using `break`/`continue` outside a loop or `return` outside a
function is an error with status 1, not a silent no-op.

### D-0073 — Traps are strings dispatched between commands
**2026-11-19 · accepted**

`trap ACTION EXIT|INT|TERM` stores the action string on the shell.
`trap - SIG` clears it; `trap` with no arguments lists them. Signal handlers
still only set a flag. `INT`/`TERM` traps are dispatched after each command
(`wl_exec_node`) and, for `INT`, when the read loop is interrupted; `TERM` is
installed only when a trap exists. The `EXIT` trap runs at shutdown with the
original status preserved unless the trap itself calls `exit`. Trap dispatch is
suppressed while another trap runs (`trap_depth`), so a trap cannot re-enter
itself.

Limitation: a trap for `INT` is not run asynchronously while a long external
command runs; it is dispatched after the command or at the next prompt. That is
observable and documented (D-0076).

### D-0074 — `set` options are implemented with explicit approximations
**2026-11-19 · accepted**

`set -e/-u/-x/-n`, `set +e...`, `set -- args`, and bare `set` (print variables)
are supported. The rules:

* `-e` is suppressed while evaluating an `if`/`while`/`until` condition, the
  non-final side of `&&`/`||`, and a `!` pipeline. It stops the shell in
  non-interactive mode; an interactive shell records the status and keeps
  going. This is the common case, not a proof of the POSIX clause.
* `-u` errors on expanding an unset *named or positional* parameter that has no
  default operator; `$@`, `$*`, `$?`, `$$`, `$#` remain valid.
* `-x` prints `+ command args` to stderr before dispatch.
* `-n` makes every subsequent node a no-op (read but never execute); turning it
  off again is not supported.

### D-0075 — Script files and the differential harness
**2026-11-19 · accepted**

`wonderland script.sh arg...` reads the file (bounded by `WL_SOURCE_MAX`),
executes it non-interactively, and sets `$0` to the path and `$1..` to the
arguments. `-c CMD name args` follows the POSIX convention: `$0=name`,
`$1..=args`. A missing script exits 127 with the `fopen` error; a syntax error
exits 2.

`tests/diff.sh` runs every `tests/cases/*.sh` under Wonderland, dash and bash
in a clean environment and requires stdout and status to agree with both. It
skips visibly when either reference shell is missing (`WL_REQUIRE_DIFF=1`
turns that into a failure). Fourteen cases cover words, variables, splitting,
redirections, pipes, if, loops, case, functions, subshells, status, heredocs,
positional parameters and EXIT traps.

### D-0076 — Remaining shell gaps after M3b
**2026-11-19 · accepted**

* No job control (`&` is still refused), no history, no line editor, no
  completion; `Ctrl-Z` remains a no-op (D-0051).
* `local`, `read`, `exec`, `command`, `type`, `wait`, `getopts`, `umask`,
  `ulimit`, `source`/`.` are not implemented.
* `"$@"` adjacent to other text joins instead of producing multiple fields.
* Non-whitespace `IFS` collapses repeated delimiters (D-0060).
* Traps for INT/TERM are dispatched between commands, not asynchronously.
* `set -e` follows the common suppression rule, not every POSIX corner.
* Functions cannot be exported or unset individually; redefining replaces.
* `return` at top level is an error rather than a `exit` alias, matching dash.

### D-0051 — Ctrl-Z is a no-op until M4; non-interactive children share the group
**2026-11-19 · accepted**

Two consequences of not having a job table yet:

* Interactive children inherit SIGTSTP/SIGTTIN/SIGTTOU as ignored. A stopped
  child could not be resumed (`waitpid` without `WUNTRACED`, no `fg`), so the
  terminal would hang with no recovery path. Ctrl-Z therefore does nothing to
  a command until M4 implements suspend/resume. This is a deliberate
  divergence, tested in `tests/pty/pty_test.c`.
* When stdin is not a terminal (`-c`, pipes, here-docs), children stay in the
  shell's process group and the terminal is not handed over. A child in its
  own group without terminal ownership would stop on the first `read` from the
  tty (SIGTTIN) with nobody to continue it.

M4 removes both: it adds the job table, `WUNTRACED`/`SIGCONT`, `fg`/`bg`, and
restores default stop dispositions in children.
