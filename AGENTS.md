# AGENTS.md

Contract for anyone — human or agent — changing this repository. Read it
before the first edit of a session. If a task conflicts with this file, stop
and ask; do not silently override it.

## 1. Project

Wonderland is a Unix shell written in C17 for Linux, using libc and POSIX
only, with an optional narrative layer inhabited by personas. The ordering is
not negotiable:

> **A proper shell first, a living inhabited world second.**

Repository docs are normative:

| File | Authority over |
|------|----------------|
| `docs/original-brief.md` | intent (verbatim source brief) |
| `docs/DECISIONS.md` | what has been decided, numbered and dated |
| `docs/ARCHITECTURE.md` | module boundaries, ownership, isolation |
| `docs/GRAMMAR.md` | accepted syntax and AST |
| `docs/CONTENT_FORMAT.md` | pack validity |
| `docs/COMPATIBILITY.md` | what may be called supported |
| `docs/PERSONAS.md` | persona model, events, frequencies |
| `docs/ROADMAP.md` | milestone order and exit criteria |
| `docs/RISKS.md` | known failure modes and tripwires |

Current status: Milestone 6 (2026-11-19). The shell is usable for scripts and
daily interactive work; 14 differential cases agree with dash and bash. The
narrative layer now loads content packs: five personas (Alice, Grimm,
Nietzsche, Kafka, Cioran) with original lines, selection with frequency,
cooldowns and budget, `possess`, and placeholder rendering. Plain mode loads no
packs. Missing: M7 mechanics, the rest of the cast, Bash extensions, `local`,
`exec`, `kill`/`disown`. It is not installed in `/etc/shells` and must not be
until the dogfooding gate is met. Content packs are normative: see
`docs/CONTENT_FORMAT.md` and `packs/core/`; never mark unverified text as
canonical.

## 2. Non-negotiable safety rules

1. Narrative code never modifies execution: not argv, env, status, flow, jobs,
   terminal, or cwd.
2. Persona hooks never change exit codes. Hooks run *after* the status is final
   and receive `const struct wl_event *`.
3. Narrative output never reaches stdout and never runs when stdout-facing
   behaviour could be observed by a pipeline, script, or `-c`.
4. Plain mode (`--plain`, `WONDERLAND_PLAIN=1`) disables narrative behaviour
   completely — no packs loaded, no hooks called, no tty switching.
5. Terminal settings are restored on every exit path, including signals.
6. Signal handlers do one thing: write a byte to the self-pipe, or (until a
   poll loop exists) set a `sig_atomic_t` flag. Nothing else. Ever.
7. A malformed pack, corrupt state file, or failed narrative allocation must
   leave a working shell.
8. No unbounded recursion: parser depth, expansion depth, and pack size have
   limits, and limits produce diagnostics, not crashes.
9. `rest` is a shell-state checkpoint, not process resurrection. Never claim
   otherwise in code, docs, or messages.
10. Never claim compatibility that a test does not demonstrate.

If a change would violate one of these, the change is wrong, even if it makes
the world feel better.

## 3. Build, test, lint

```sh
make                  # build build/wonderland
make test             # unit + CLI + pty + isolation + differential tests (75/142/14)
make diff             # differential cases only (needs dash and bash)
make asan             # rebuild with ASan/UBSan and run the same suite
make memcheck         # unit suite under valgrind (developer tool)
make lint             # cppcheck over src/ (developer tool)
make clean            # remove build/
make install          # ~/.local/bin/wonderland (PREFIX=... to override)
```

`make install` is user-local by design: no `/etc/shells` entry, no `chsh`
(amendment 4).

Requirements: C17 compiler (GCC or Clang), GNU Make, POSIX shell for
`tests/run.sh`. Optional but recommended: `dash` for differential tests,
`valgrind` for leak checks, `clang-format` for consistency. This environment
has `dash` 0.5.13.5, `valgrind` 3.25.1 and `cppcheck` 2.22.0; the differential
harness itself is still M2–M3 work.

Rules:

* Zero warnings at `-Wall -Wextra -Wpedantic`. `-Werror` is not default (see
  D-0035) but a new warning is a review failure.
* Run `make test` before every commit. Run `make asan` before every commit that
  touches parsing, expansion, content loading, or state files.
* Never commit with a red `make test`. If it is red for unrelated reasons, say
  so explicitly in the commit or handoff.
* No new build dependency without a decision entry. No CMake, no autotools,
  no test framework, no JSON library.

## 4. C style

* C17. `-std=c17`, `_POSIX_C_SOURCE=200809L`. `_DEFAULT_SOURCE` only with a
  comment explaining which symbol needed it.
* 4 spaces, no tabs. 80 columns where practical; 100 is the hard ceiling.
* `snake_case` functions and variables, `UPPER_SNAKE` macros/enums, `struct
  wl_*` for public types, `static` for everything not in a header.
* Braces on the same line for functions *and* blocks (K&R, consistent with the
  existing sources). One declaration per line.
* `bool` from `<stdbool.h>` for predicates; status codes are `int` or a small
  enum, never a bare `bool` where three outcomes exist.
* No `goto` except a single cleanup label per function when it removes
  duplicated frees. No `setjmp`/`longjmp` in core.
* Byte-oriented: use `size_t` for lengths, `unsigned char` when inspecting
  bytes, explicit ASCII comparisons in the lexer.
* Comments explain *why*, invariants, and ownership. Do not narrate code.
* Every public header is include-guarded, includes what it uses, and does not
  include `<stdio.h>` unless it really needs `FILE`.
* Keep functions under ~40 lines where the shape permits; if a function needs
  a diagram, move the diagram to a comment or split it.

### Sanitizers

`make asan` builds with `-fsanitize=address,undefined -fno-omit-frame-pointer
-g` into `build/asan/`. ASan findings are bugs, not noise. Leaks in one-shot
paths may be documented; leaks in the prompt loop may not.

## 5. Memory ownership

Canonical conventions (`docs/ARCHITECTURE.md` §6):

| Form | Meaning |
|------|---------|
| `wl_x_new()` | caller owns; free with `wl_x_free()` |
| `wl_x_free(NULL)` | allowed, no-op |
| getter / `wl_x_borrow()` | borrowed; do not free, do not retain past the owner |
| `wl_x_dup()` | caller-owned copy |
| `wl_arena_alloc()` | owned by the arena; freed with the arena only |
| `const T *` parameter | callee must not free, mutate, or retain |

Rules:

* Every allocation belongs to a stated lifetime: line, command, session, or
  process. If you cannot state it, you do not have a design yet.
* Parse/AST memory lives in the per-line arena and is never freed individually.
* Callers own their arguments unless the header says otherwise, in writing.
* Allocation failure is reported and unwound. Never `abort()` on OOM in the
  shell; never ignore it either.
* `free()` the pointer you allocated, not an interior pointer. No casting away
  `const` to free.
* Narrative allocates nothing the shell must free.

## 6. Module dependency rules

Allowed edges only (`docs/ARCHITECTURE.md` §3):

* `core` → libc only.
* `term`, `line`, `lex`, `ast`, `parse`, `expand`, `jobs`, `content` → `core`
  and modules below them in the graph.
* `exec` → `core`, `ast`, `expand`, `builtins`, `jobs`, `term`.
* `builtins` → `core`, `exec` (interface), `jobs`, `content`, narrative ABI.
* `narrative` → libc and its own headers. **Nothing else.**
* `main` wires everything.

Hard rules for `src/narrative/`:

* Do not include `core/`, `exec/`, `jobs/`, `builtins/`, `parse/`, `ast/`,
  `expand/`, `line/`, `term/`, or `content/` headers.
* Do not call `fork`, `exec*`, `system`, `popen`, `setenv`, `putenv`,
  `unsetenv`, `chdir`, `dup2`, `kill`, `tcsetpgrp`, `exit`, or write to
  `stdout`.
* Do not add a symbol that `exec` or `builtins` must resolve; the link probe
  (`make check-isolation`) exists to catch exactly that.
* Do not take a `struct wl_shell *`. The only inputs are `const` events and
  immutable packs.

Do not invert an edge "just this once". If the data you need is not in the
event, add a field to the event — do not reach into the shell.

## 7. Plain mode and stdout purity

* Any new output must be classified before it is written: **command output**
  (inherited fds), **shell diagnostic** (stderr, `wonderland: ...`), or
  **narrative** (stderr/`/dev/tty`, interactive only).
* Narrative is never written by library code that could run in `-c` or script
  mode.
* The prompt is written to the terminal, not blindly to stdout; it must be
  suppressed when stdin/stdout is not a tty.
* No ANSI escapes in output that is not a tty. No DEC private modes outside
  Wonderland-owned presentation.
* Never write to the terminal while a foreground job owns it.
* Tests must capture stdout and stderr separately; a test that merges them
  cannot prove purity and is not acceptable for execution or narrative work.

## 8. Content rules

* Three categories only: `canonical`, `quotation`, `original`; three statuses:
  `verified`, `unverified`, `original`.
* `canonical` requires `status = verified` plus work, source, author and
  verifier. The loader rejects anything else. Do not "temporarily relax" this.
* Never commit Black Souls dialogue, or any quotation whose source you have not
  checked, or modern translations (translation copyright is real).
* Never attribute invented words to a real person. If you write a line about
  Nietzsche's worldview, it is `original` and it says so.
* Example packs in `packs/examples/` contain original lines only.
* No filler generation. Twenty good lines beat three hundred templated ones.
  A persona that cannot be recognised with its name hidden is not ready.
* Packs cannot execute anything, format strings, or emit escape bytes. Placeholders
  are limited to the enumerated set in `docs/CONTENT_FORMAT.md` §6.

## 9. Per-milestone workflow

For each feature or milestone slice:

1. **Inspect.** Read the relevant docs and the actual files. Check `pwd`,
   `git status`, and recent commits. Do not assume a component exists because
   it was planned.
2. **Explain.** State the proposed change in a few sentences, including what
   you are deliberately not doing. Get agreement if the change alters a
   documented decision; otherwise proceed.
3. **Implement one coherent feature.** No bundled refactors, no opportunistic
   cleanups, no drive-by renames. If you find unrelated bugs, report them.
4. **Add tests.** Unit tests for logic, `tests/cases/` for shell behaviour,
   pty tests for interactive paths, positive *and* failure cases.
5. **Run the suite.** `make test`; `make asan` for memory-relevant work.
6. **Check regressions.** Green is not enough if a test was weakened. Diff the
   test count and the goldens.
7. **Review memory and error handling.** Every new allocation path, every
   early return, every partially-initialised struct.
8. **Report.** Files changed, decisions made, tests added, limitations known.
9. **Hand off.** Leave the next person a working tree and an honest paragraph
   about what is unfinished.

Stop and ask when: a documented decision must change, a safety rule is in the
way, the milestone order is being violated, or the work would need a new
dependency.

## 10. Commits

Conventional Commits, from the `commits` skill:

```
<type>(<scope>): <imperative summary, <= 50 chars>

<body: what and why, wrapped at ~72 chars>

Refs #123
```

* Types: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`,
  `chore`. Scopes: `core`, `term`, `line`, `lex`, `parse`, `ast`, `expand`,
  `exec`, `jobs`, `builtins`, `narrative`, `content`, `docs`, `build`, `repo`.
* One logical change per commit. Docs and code may share a commit only when the
  code change *is* the decision being documented.
* Subject imperative: "add", not "added".
* Never commit secrets, editor state, build output, or generated binaries.
* Check `git status` before `git add`; stage paths explicitly rather than
  `git add -A` in a tree you have not inspected.
* Do not rewrite published history.

## 11. Do not implement speculative features

The brief and `docs/DECISIONS.md` D-0041 are explicit. Specifically, do not
start, "while you're in there":

* `[[ ]]`, arrays, process substitution, `coproc`, `select`, `time`;
* a plugin ABI, a scripting layer, a network feature, a colour theme engine;
* mirror mode, respawn, rare cycle events (M7);
* a new abstraction for a single call site;
* a config file, a cache, or a database to solve a problem that does not exist
  yet;
* performance work without a measurement.

If a feature is not in the current milestone's exit criteria, it waits. The one
sanctioned temporary shortcut is the M1 line reader (D-0037), and it is
deleted, not extended.

## 12. When in doubt

* Shell correctness beats narrative charm.
* Silence beats a forced line.
* A diagnostic with a byte column beats a mysterious failure.
* A documented limitation beats an undocumented bug.
* Ask, rather than guess at a design decision.
