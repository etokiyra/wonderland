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

None yet.

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
