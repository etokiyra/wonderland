# Wonderland — Design

Status: design phase. This document records intent, not implemented behaviour.
Nothing in the shell is implemented yet beyond a bootstrap binary that answers
`--version` and accepts `--plain` (see `README.md`).

Source of truth: `docs/original-brief.md`. Where a decision differs from the
brief, the amendment is recorded in `docs/DECISIONS.md` and repeated here.
Project name: **Wonderland** (formerly ABGRUND).

## 1. What Wonderland is

A real, daily-drivable Unix shell written in C17 against libc and POSIX only,
for Linux, that carries a second, optional identity: an inhabited dark
fairy-tale world with a cast of personas who react to what happens in the
shell.

The two halves are ordered, not equal:

> A proper shell first, a living inhabited world second.

The narrative layer is a *reader* of events. It is never a participant in
execution. If the choice is ever "better story" versus "correct shell", the
shell wins, every time, without discussion.

### What Wonderland is not

* Not a toy interpreter, not a game that pretends to be a shell.
* Not a terminal emulator, not a screen/TUI application.
* Not a Bash clone, not a POSIX conformance badge we hand out for free.
* Not a quotes-of-the-day printer. Silence is a valid and frequent response.
* Not dependent on a daemon, database, network service, or scripting runtime.
* Not a system shell by default. It does not go into `/etc/shells` and is not
  set via `chsh` (amendment 4).

## 2. Design principles

1. **Execution purity.** Command text, argv, environment, exit status, job
   control and terminal ownership are decided by the shell core alone.
2. **Structural isolation over discipline.** The narrative layer is a separate
   static library that cannot see execution headers or mutable shell state. The
   rule is enforced by the build and by scripts, not by good intentions.
3. **stdout is sacred.** Anything a script, pipeline or program can observe on
   stdout is shell output or command output. Narrative text never touches it.
4. **Plain mode is a hard switch.** Not a "reduce decoration" dial. `--plain`,
   `WONDERLAND_PLAIN=1` and any non-interactive invocation disable the world
   entirely.
5. **POSIX first, extensions later, claims last.** Implement POSIX shell
   behaviour properly, add selected Bash convenience features afterwards, and
   only claim what the test suite actually verifies.
6. **Boring internals.** Recursive descent, explicit ownership, arenas per
   parse unit, no GC, no hidden global state beyond one shell struct passed
   explicitly.
7. **Small and legible.** Prefer a 40-line function with an obvious failure
   path to an abstraction with a name.
8. **The world is optional and cheap.** Loading no packs, an empty library, or
   a corrupt state file must leave a fully working shell.
9. **Content honesty.** Canonical text, sourced quotation and original writing
   are three different things and are labelled as such, always.
10. **Restraint.** No ASCII art banners, no roleplay on every command, no
    theatrics over a failing `grep`.

## 3. Narrative contract

Amendments 2 and 3 define the contract; the brief defines the intent.

* Narrative output is emitted **only** when stdin *and* stderr are terminals,
  i.e. an interactive session, and only when neither `--plain` nor
  `WONDERLAND_PLAIN=1` is set, and only when the active configuration enables
  the world.
* It is written to **stderr** by default, or to `/dev/tty` when the
  configuration says so (useful when stderr is redirected but the session is
  still interactive). It is **never** written to stdout.
* `wonderland -c '...'` and `wonderland script.sh` are **always plain**, even
  if both stdin and stderr are terminals.
* Builtins `possess`, `rest`, `go-to-alice` and `exit` own every state change
  that the world can observe. Personas cannot change persona, checkpoint,
  cycle counter, or exit status.
* A persona hook receives an immutable `struct wl_event` and returns text (or
  silence). It performs no I/O, allocates nothing that the shell must free,
  and cannot fail in a way that affects the command.
* The narrative layer must never delay the prompt by more than a negligible,
  bounded amount of work (selection is O(candidates) over an in-memory pack).

## 4. Non-negotiable safety rules

These are the brief's section 13, restated with the enforcement mechanism that
makes each one checkable.

| # | Rule | Enforced by |
|---|------|-------------|
| 1 | Narrative code never modifies execution behaviour | `libwl_narrative.a` cannot include execution/state headers; link probe with `-Wl,--no-undefined`; review |
| 2 | Persona hooks never change exit codes | Hooks are called *after* the status is stored; return value is text only |
| 3 | Narrative output never contaminates pipelines or redirected output | Narrative text goes to stderr/`/dev/tty`; stdout path contains no narrative call sites |
| 4 | Plain mode genuinely disables narrative behaviour | Single gate `wl_narrative_enabled()`; packs not loaded, hooks not called, no tty switching |
| 5 | Terminal settings restored after interruption and abnormal exit | `term` module saves a `termios` snapshot; `atexit` + signal paths restore; panic-free cleanup path |
| 6 | Child processes managed correctly | One reaper, `waitpid` loop, process groups, no `SIGCHLD` handler doing real work |
| 7 | Signal handlers do not do unsafe work | Handlers `write()` one byte to a self-pipe; all logic in the main loop |
| 8 | Malformed persona packs do not crash the shell | Loader returns diagnostics, discards the pack atomically, shell continues |
| 9 | Persistent state recoverable or safely ignored | Versioned header, checksum, atomic write via `rename()`, corrupt file ⇒ start fresh |
| 10 | Interactive applications receive proper terminal control | Foreground process group gets `tcsetpgrp`; shell ignores `SIGTTOU`/`SIGTTIN` as required |
| 11 | Memory ownership explicit | Arena-per-parse, documented `_new`/`_borrow`/`_dup` conventions, ASan/UBSan build in CI |
| 12 | Errors reported clearly | Diagnostics carry `file:line:col`, a source excerpt and a caret |
| 13 | No silent compatibility claims | `docs/COMPATIBILITY.md` lists tested / untested / unsupported; `--version` output stays factual |

Additional operational rules adopted here:

14. The shell is not registered as a login shell and not installed into
    `/etc/shells` until dogfooding criteria in `docs/ROADMAP.md` are met.
15. Narrative output is suppressed while a foreground job owns the terminal,
    and after the terminal is known to be in an unknown state.
16. The shell never exits because a command failed. `rest`/death screens are
    presentation only.

## 5. Plain mode

| Input | Effect |
|-------|--------|
| `--plain` | Narrative disabled for this process |
| `WONDERLAND_PLAIN=1` | Narrative disabled for this process (any non-empty value other than `0` is truthy; document the accepted forms) |
| `wonderland -c 'cmd'` | Always plain; `--plain` is accepted but redundant |
| `wonderland file.sh` | Always plain |
| stdin or stderr not a tty | Plain |
| `--plain` plus `WONDERLAND_PLAIN=0` | Plain wins; there is no "un-plain" switch at runtime |

Precedence: `--plain` > `WONDERLAND_PLAIN` > tty detection > configuration
default. A future `possess --plain`/`--world` runtime toggle may only *tighten*
this ordering (it can disable, never re-enable, when any of the above says
plain). Recorded as an open question in `docs/DECISIONS.md`.

The historical name `ABGRUND_PLAIN` is dead. It is not read, not aliased, and
not mentioned in user-facing text. (Amendment 1.)

## 6. Identity and tone

Atmosphere: Black Souls' oppressive, elegant decay; *Alice in Wonderland*'
arbitrary rules; dark fairy tales; existentialist and pessimist literature;
psychological horror used sparingly.

Constraints on the aesthetic:

* Dark by default in intent, but the shell **sets no colours it does not own**:
  no background repainting, no palette hijacking, no escaping that survives
  into a pipe.
* Prompt is practical: cwd, exit status, active persona marker, optional time.
  Readable over SSH, in a 80×24 terminal, and in a monochrome terminal.
* No banner larger than a few lines, shown at most once per session.
* Silence is the default state of the world. A persona speaks when the moment
  earns it, not on a schedule.
* The world never speaks over a program that owns the screen.

## 7. Cast strategy

Five maximally distinct voices first — Alice, Grimm, Nietzsche, Kafka, Cioran
(amendment 6) — then expansion toward 20–30. Differentiation is a hard
requirement: a line must be attributable to its persona with the name hidden.
See `docs/PERSONAS.md` for the model, the voice tests and the line targets
(main characters 200–300+, major philosophers 100–200+, secondary 50–100+).

## 8. Content ethics

Three categories, enforced by the loader (`docs/CONTENT_FORMAT.md`):

* **canonical** — real Black Souls material, verified, sourced. Never
  fabricated. Never committed to this repository without a redistribution
  right.
* **quotation** — real words by real people, with author, work, source and
  translation provenance. Verified or explicitly marked unverified; unverified
  quotations can never masquerade as verified or canonical.
* **original** — written for Wonderland, marked as original, never presented as
  a quotation.

Example packs shipped in this repository contain **original lines only**
(amendment 5). Any pack containing Black Souls text or third-party quotations
lives outside the repository, in the user's own pack directory, and is the
user's responsibility to source and license.

## 9. Scope discipline

Not in scope for the first releases: Windows, macOS, non-x86 assumptions that
require porting work, network features, plugin ABIs, a package manager, a
database, a full bash emulation, `set -o vi`, programmable completion, and
anything that exists mainly because it sounds thematic. Themed mechanics are
admitted only when they are observational or thin builtins (amendment 9).

## 10. Definition of done (project level)

Wonderland is "a shell" when all of the following are true, tested by the
suite, and dogfooded:

* POSIX pipeline/list/redirection/expansion semantics on the supported subset,
  with the test matrix in `docs/COMPATIBILITY.md` passing.
* Job control usable in a real terminal: Ctrl-C, Ctrl-Z, `jobs`, `fg`, `bg`,
  no orphaned process groups, terminal restored after every path.
* A line editor that survives resize, SSH, and a stuck terminal.
* Scripts written for dash run unmodified, or fail in ways we document.
* The narrative layer can be removed from the build without breaking the
  shell, and can be enabled without changing a single test expectation on
  stdout or exit status.
* Zero known memory errors under ASan/UBSan on the corpus.
* Documentation matches behaviour.

## 11. Open questions

Recorded with owners in `docs/DECISIONS.md`; the ones that shape design:

* License (no `LICENSE` file yet, deliberately).
* Whether `WONDERLAND_PLAIN` rejects values other than `0`/`1`.
* Whether narrative output defaults to stderr or `/dev/tty`.
* Install prefix and whether `make install` exists before M8.
* Whether content packs are versioned independently or with the binary.
* `rest`/`respawn` storage format and location (`$XDG_STATE_HOME`?).
