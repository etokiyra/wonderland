# Wonderland — Architecture

Status: design. Only the build skeleton exists; this document is the contract
that Milestone 1 onwards must implement.

## 1. Shape of the program

One process, one thread, one `struct wl_shell` threaded explicitly through the
core. No global mutable state; the only globals are `const` tables and the
async-signal-safe self-pipe descriptors needed by signal handlers.

Build products:

```
build/
  wonderland            # the binary
  libwl_shell.a         # everything except narrative
  libwl_narrative.a     # the world (isolated)
  wl_tests              # unit test runner
  narrative_probe       # link probe proving libwl_narrative.a is self-contained
```

The binary links `libwl_shell.a` and `libwl_narrative.a`; the shell libraries
never link against each other. The narrative library is linked *by* `main.c`
only, and only through `<wonderland/narrative_abi.h>`-style headers that
contain no shell state.

## 2. Subsystems

The brief names fifteen subsystems. They map to directories as follows. Two
adjustments are deliberate and recorded in `docs/DECISIONS.md`: history and
completion live with the line editor (they share the readline state machine),
and configuration lives in `core` (it is inert data, not a subsystem with its
own lifecycle). `src/main.c` is added as the process entry point.

| # | Subsystem | Directory | Key headers | Responsibility |
|---|-----------|-----------|-------------|----------------|
| 1 | Terminal interface | `src/term/` | `term/tty.h` | raw mode, `termios` save/restore, window size, `/dev/tty`, `tcsetpgrp` wrappers |
| 2 | Input / line editor | `src/line/` | `line/editor.h`, `line/history.h`, `line/complete.h` | key decoding, cursor, history ring, completion, resize handling |
| 3 | Lexer | `src/lex/` | `lex/lex.h`, `lex/token.h` | bytes → tokens with spans and quoting flags, heredoc body collection |
| 4 | Parser | `src/parse/` | `parse/parse.h` | tokens → AST, diagnostics, heredoc queue, recursion limits |
| 5 | AST | `src/ast/` | `ast/ast.h`, `ast/arena.h` | node structs, constructors, traversal, arena allocation, dump for tests |
| 6 | Expansion | `src/expand/` | `expand/expand.h` | parameter/command/arithmetic/tilde expansion, field splitting, globbing, quote removal |
| 7 | Execution | `src/exec/` | `exec/exec.h` | builtin dispatch, `fork`/`exec`, pipelines, redirections, `&&`/`\|\|`/`;`/`&`, subshells |
| 8 | Process & job control | `src/jobs/` | `jobs/job.h`, `jobs/signal.h` | process groups, job table, `waitpid`, terminal ownership, self-pipe signals |
| 9 | State & environment | `src/core/` | `core/shell.h`, `core/var.h`, `core/arena.h`, `core/str.h` | variables, environment snapshots, options, `$?`, `$$`, cwd, config, diagnostics |
| 10 | Builtins | `src/builtins/` | `builtins/builtins.h` | `cd`, `exit`, `export`, `unset`, `jobs`, `fg`, `bg`, `possess`, `rest`, `go-to-alice`, … |
| 11 | History & completion | `src/line/` | `line/history.h`, `line/complete.h` | persisted history file, tab completion over commands/paths |
| 12 | Persona engine | `src/narrative/` | `narrative/persona.h` | persona records, selection, cooldowns, anti-repetition, line rendering |
| 13 | Narrative events | `src/narrative/` | `narrative/event.h` | event structs, dispatch, hook table, silence policy |
| 14 | Content pack loader | `src/content/` | `content/pack.h` | parse/validate packs, produce immutable `wl_persona_pack`, never executes content |
| 15 | Configuration | `src/core/` | `core/config.h` | defaults, config file, env overrides, plain-mode resolution |

## 3. Dependency rules

```
            ┌──────────────────────────────┐
            │            main              │
            └───────┬──────────────┬───────┘
                    │              │
        ┌───────────▼───────┐  ┌───▼───────────────┐
        │  libwl_shell.a    │  │ libwl_narrative.a │
        │  core ← term      │  │ persona + events  │
        │  lex ← parse ← ast│  │ (no shell headers)│
        │  expand ← exec    │  └───────────────────┘
        │  jobs ← exec      │            ▲
        │  builtins ← exec  │            │ immutable packs
        │  content ─────────┼────────────┘
        └───────────────────┘
```

Allowed edges (anything else is a bug):

* `core` depends on libc only.
* `term`, `line`, `lex`, `ast`, `parse`, `expand`, `jobs`, `content` depend on
  `core` and on modules below them in the graph above. `line` additionally
  reads the SIGINT flag from `jobs/signal.h`: one function, no shell state, no
  mutation.
* `exec` depends on `core`, `ast`, `expand`, `builtins`, `jobs`, `term`.
* `builtins` depends on `core`, `exec` (interface only), `jobs`, `content`,
  `narrative` (ABI only).
* `narrative` depends on libc and its own headers only.
* `content` depends on `core` and the narrative *data* headers
  (`narrative/persona.h`, `narrative/event.h`), never on `narrative/persona.c`
  internals and never on `exec`.
* `main` depends on everything, wires it, and owns the event loop.

### 3.1 Narrative isolation mechanism

Four independent mechanisms, because one is not enough:

1. **Separate archive.** `src/narrative/*.c` compiles into `libwl_narrative.a`
   with its own `NARRATIVE_CFLAGS`, include path restricted to
   `include/wonderland/narrative` (plus system headers). It cannot resolve
   `#include "core/shell.h"` because that directory is not on its include path.
2. **Header hygiene.** `include/wonderland/narrative/*.h` may include only C
   standard headers and other `narrative/` headers. They declare opaque
   handles and plain data; no `struct wl_shell`, no `FILE *` to stdout, no
   function pointer that reaches execution code.
3. **Link probe.** `make check-isolation` links `libwl_narrative.a` against a
   two-line stub `main` with `-Wl,--no-undefined`. If the narrative archive
   ever references a shell symbol, the probe fails. This is a real,
   mechanical proof of self-containment, not a grep.
4. **Source audit script.** `scripts/check-isolation.sh` rejects forbidden
   identifiers in `src/narrative/`: `fork`, `exec*`, `system`, `popen`,
   `setenv`, `putenv`, `unsetenv`, `chdir`, `dup2`, `kill`, `tcsetpgrp`,
   `printf` to `stdout`, `exit`, and includes of `core/`, `exec/`, `jobs/`,
   `builtins/`, `parse/`, `ast/`, `expand/`, `line/`, `term/`, `content/`.
   It also rejects `src/exec/` and `src/builtins/` including `narrative/*.h`
   except the ABI header, so the dependency cannot be inverted.

All four were exercised on 2026-11-18 by temporarily adding broken narrative
translation units; each mechanism failed in the expected way and the tree was
restored afterwards (docs/DECISIONS.md D-0043).

### 3.2 Why narrative returns text instead of printing

`wl_narrative_react()` returns a `struct wl_line { const char *text; size_t
len; }` that points into loaded pack storage (borrowed, valid for the session).
The shell decides whether and where to print it. Consequences:

* Structural stdout purity: there is no code path from narrative to fd 1.
* Plain mode is a single `if` in the shell, not a flag checked in twenty
  persona functions.
* Testability: narrative output can be asserted without capturing a terminal.
* Failure isolation: a persona that produces nothing returns `{NULL, 0}`; the
  shell prints nothing. No persona can block on I/O.

## 4. Execution flow

```
main
 ├─ config/plain resolution, tty detection
 ├─ term_init (save termios) ── atexit/signal restore
 └─ loop
     ├─ read line   (line editor if interactive, fgets/getline otherwise)
     ├─ lex         → tokens
     ├─ parse       → AST in per-line arena
     ├─ expand      → argv/redirs in per-command arena
     ├─ exec        → status (int)         ← the only writer of $?
     ├─ jobs_update → reap children, mark done (no narrative calls)
     ├─ narrative   → (interactive, enabled) hook(name, const event) → line → stderr/tty
     ├─ prompt      → render (shell-owned only)
     └─ arena reset (per-line), keep persistent state
```

Ordering rules that make the contract true:

* The event is built *after* `status`, cwd and job deltas are final.
* The event is `const`; the hook cannot reach `wl_shell`.
* Narrative runs after the status is stored and before the prompt; a crashed
  or silent narrative layer is indistinguishable from a plain shell.
* If a foreground job was running, no narrative runs for the duration of that
  job (nothing is written while a program owns the screen).

### 4.1 Implemented at M1
| Step | File |
|------|------|
| option handling, `-c`, stdin, the loop | `src/main.c`, `src/core/opts.c` |
| session state, `$?`, `exit` bookkeeping | `src/core/shell.c` |
| terminal snapshot and prompt | `src/term/term.c`, `src/term/prompt.c` |
| SIGINT flag and child dispositions | `src/jobs/signal.c` |
| canonical-mode line reader | `src/line/read.c` |
| PATH resolution | `src/exec/path.c` |
| fork/exec, process group, terminal handoff | `src/exec/exec.c` |
| `cd`, `exit` | `src/builtins/builtins.c` |
| M1-only whitespace splitter (deleted in M2) | `src/exec/m1_split.c` |

Everything else in this document is still a contract, not a description. The
prompt is written to stderr; no narrative output exists yet, so stdout purity
is currently a property of the code structure rather than of a runtime gate.

### 4.2 Implemented at M2

| Step | File |
|------|------|
| byte buffer, diagnostics with carets | `src/core/buf.c`, `src/core/diag.c` |
| per-parse arena, span/AST/word types | `src/ast/arena.c`, `src/ast/ast.c` |
| stable AST dump | `src/ast/dump.c` |
| tokens, operators, here-document bodies | `src/lex/lex.c` |
| word parts, expansions syntax, quoting | `src/lex/word.c` |
| grammar, here-document queue, cmdsub sub-programs | `src/parse/parse.c` |
| continuation input units and diagnostics | `src/main.c` |
| AST-driven execution for `;`, `&&`, `\|\|`, `!`, literal words | `src/exec/exec.c` |

The M1 whitespace splitter is gone. Execution refuses constructs the parser
accepts but the process model cannot yet run (D-0057), so the two layers can
advance independently without lying about either.

### 4.3 Implemented at M3a

| Step | File |
|------|------|
| variable store and exported environment | `src/core/var.c` |
| parameter/command/arithmetic/tilde expansion, splitting, globbing | `src/expand/expand.c` |
| integer arithmetic | `src/expand/arith.c` |
| redirections, pipelines, command capture, assignments | `src/exec/exec.c` |
| `export`, `unset`, variable-aware `cd` | `src/builtins/builtins.c` |
| here-document expansion mode | `src/lex/word.c` (`wl_lex_word_heredoc`) |

Dependency note: `expand` does **not** depend on `exec`. Command substitution
is a callback (`wl_capture_fn`) supplied by the executor, which keeps the
"narrative and expansion cannot reach into process state" boundary intact
while still allowing `$(...)` to run a sub-program.

### 4.4 Implemented at M3b

| Step | File |
|------|------|
| positional-parameter frames, `$0`, `shift` | `src/core/shell.c` |
| compound execution, subshells, control flow, traps | `src/exec/exec.c` |
| function table with session-lifetime arenas | `src/exec/function.c` |
| `set`, `shift`, `trap`, `return`, `break`, `continue`, `:` | `src/builtins/builtins.c` |
| flag-only signal handlers for trapped signals | `src/jobs/signal.c` |
| script files, argument passing, EXIT trap, source tracking | `src/main.c`, `src/core/opts.c` |
| differential harness (dash + bash) | `tests/diff.sh`, `tests/cases/` |

Function bodies are the one AST that must outlive a parse unit; they are
re-parsed into their own arenas rather than keeping the per-line arena alive
(D-0070).

### 4.5 Implemented at M4

| Step | File |
|------|------|
| job table, waiting, terminal handoff, notifications | `src/jobs/job.c` |
| interactive job initialisation (`setpgid` + tty) | `src/jobs/job.c` |
| `jobs`, `fg`, `bg`, `wait` | `src/builtins/builtins.c` |
| background `&`, stopped-foreground job creation | `src/exec/exec.c` |
| `$!` | `src/core/shell.h`, `src/expand/expand.c` |
| prompt-time notification | `src/main.c` |

A background statement is one wrapper process in its own process group; a
foreground pipeline contributes several processes to one job. The shell does
not use SIGCHLD for bookkeeping: it waits synchronously for foreground jobs
and reaps with `WNOHANG` between commands (D-0080).

### 4.6 Implemented at M5

| Step | File |
|------|------|
| raw-mode editor, redraw, completion | `src/line/editor.c` |
| history storage and XDG path | `src/line/history.c` |
| SIGWINCH flag | `src/jobs/signal.c` |
| prompt override, history lifetime | `src/main.c` |
| `$'...'` decoding | `src/lex/word.c` |
| `read` | `src/builtins/builtins.c` |

The editor restores canonical settings before returning a line, so execution
never runs with a raw terminal. History is only read and written for
interactive sessions.

### 4.7 Implemented at M3.5

| Step | File |
|------|------|
| event/line ABI and selection state | `include/wonderland/narrative/persona.h` |
| Alice's twelve original lines and selection | `src/narrative/narrative.c` |
| plain/interactive gate, event construction, stderr output | `src/main.c` |

The narrative archive is linked into the binary and the unit tests, and is
still the subject of the `--whole-archive --no-undefined` probe. It has no
access to `struct wl_shell`, performs no I/O, and returns borrowed text; the
shell's single `wl_emit_narrative` call is the only path from event to
terminal.

### 4.8 Implemented at M6

| Step | File |
|------|------|
| pack parser, provenance validation, atomic rejection | `src/content/pack.c` |
| persona/line data model (ABI) | `include/wonderland/narrative/persona.h` |
| selection, cooldowns, budget, anti-repetition, rendering | `src/narrative/narrative.c` |
| pack discovery, gating, event construction | `src/main.c` |
| `possess`, `possess -l` | `src/builtins/builtins.c` |
| core cast | `packs/core/*.pack` |
| pack installation | `Makefile` (`$(PREFIX)/share/wonderland/packs`) |

Content loading happens only for interactive, non-plain sessions; `plain` is
resolved before any pack is read. The loader owns pack memory for the session
and the narrative state only borrows it.

### 4.9 Implemented at M7

| Step | File |
|------|------|
| versioned/checksummed state, atomic write, hex values | `src/core/state.c`, `include/wonderland/core/state.h` |
| cycle increment, corrupt-file note, persona persistence | `src/main.c` (`wl_library_open`/`wl_library_close`) |
| `rest`, `respawn`, `go-to-alice`, shared exit path | `src/builtins/builtins.c` |
| job start/finish counters and last command | `src/jobs/job.c`, `include/wonderland/jobs/job.h` |
| Black Rabbit event emission | `src/main.c` (`wl_emit_job_starts`/`wl_emit_job_finishes`) |
| death screen, mirror prompt fragment | `src/main.c` (`wl_emit_death_screen`, `wl_apply_mirror`) |
| Starship-style left/right prompt, git/path/time segments | `src/term/prompt.c` |
| right-prompt alignment, visible-width cursor math, OSC 133 mark | `src/line/editor.c`, `src/main.c` |
| state tests | `tests/unit/test_state.c` |

The state file is owned by the shell (`struct wl_state library`) and written
by main and the builtins. It lives in `core/` because it is inert data with no
narrative dependency. The job module still knows nothing about narrative: it
only exposes monotonic counters and a borrowed printable command, and main
performs the emission through the same gate as every other event.

## 5. Core data model

```c
struct wl_shell {                 /* one instance, stack-allocated in main */
    struct wl_vars    vars;       /* shell vars + exported set */
    struct wl_opts    opts;       /* set -e/-u/-x, plain, world on/off */
    struct wl_jobtab  jobs;
    struct wl_arena  *persist;    /* lifetime of session */
    struct wl_arena  *line;       /* reset per command line */
    struct wl_persona_state persona;   /* opaque to narrative code */
    int               last_status;
    pid_t             shell_pgid;
    struct wl_term    term;
    struct wl_config  cfg;
    struct wl_packset packs;      /* immutable after load */
    bool              interactive;/* stdin && stderr are ttys, and not -c/file */
};
```

* **Strings**: `struct wl_str { char *data; size_t len; }` — length-carrying,
  NUL-terminated for convenience, never assumed to be UTF-8. Bytes are the
  unit; columns in diagnostics are byte columns (documented).
* **Vectors**: typed macro over `{void *data; size_t len, cap;}` with a growth
  function that reports failure; no silent truncation.
* **AST**: nodes are pointers into the per-line arena; no node is individually
  freed; no node is mutated after parsing except the documented expansion
  scratch fields (separate struct, not in AST).
* **Event**: `struct wl_event` is a value struct of immutable fields (`kind`,
  `status`, `signal`, `job_id`, `cwd`, `cmdline`, `argv0`, `duration_ms`,
  `cycle`), passed as `const struct wl_event *`. No pointers to shell-owned
  strings outlive the call.
* **Packs**: loaded once into a session-long arena; records are `const` after
  load; reload replaces the whole arena (no partial mutation).

## 6. Memory conventions

Ownership is encoded in names and enforced by review and ASan:

| Suffix / form | Meaning |
|---------------|---------|
| `wl_x_new(...)` | returns owned pointer, freed with the matching `_free` |
| `wl_x_free(x)` | accepts NULL; idempotent on the same pointer |
| `wl_x_borrow(...)` / plain getter | borrowed, valid as long as the owner lives; never freed by the caller |
| `wl_x_dup(...)` | caller-owned copy |
| `wl_arena_alloc(a, n)` | owned by `a`; freed only with the arena |
| `const T *` parameter | callee must not free, retain, or mutate |

Rules:

* No `malloc` in headers/inline code; allocation goes through `core/alloc.c`
  so failure behaviour is uniform (report, unwind, never abort silently).
* Every allocation site states its lifetime: line, command, session, or
  process. Anything unclear is a review blocker.
* `free(NULL)` is fine; double free and use-after-free are bugs, not
  defensiveness targets.
* No ownership transfer hidden inside a getter.
* Narrative allocates nothing the shell frees; packs are owned by `content`.

## 7. Error handling

* Library functions return `bool` (or a status enum) and fill a
  `struct wl_diag { const char *file; int line, col; char msg[256]; }` passed
  by the caller. No global `errno`-only error reporting, no `setjmp` in core.
* Fatal-at-parse errors abort the current line only; the shell continues
  (interactive) or exits 2 (script), matching `sh` expectations.
* Builtins return an exit status; `0` success, `1`/`2` per POSIX conventions.
* Allocation failure inside the narrative path degrades to silence.
* `errno` is captured immediately after the failing call; message construction
  uses `strerror` once, never after intervening calls.

## 8. Signals and terminal ownership

* Handlers: `SIGINT`, `SIGQUIT`, `SIGTSTP`, `SIGTTIN`, `SIGTTOU`, `SIGCHLD`,
  `SIGWINCH`, `SIGHUP`. Each writes one byte to a non-blocking self-pipe and
  returns. No `waitpid`, no `printf`, no allocation in a handler.
* The main loop drains the self-pipe and performs the real work.
* The shell ignores `SIGTTOU`/`SIGTTIN`/`SIGTSTP` while it owns the terminal,
  gives the terminal to the foreground process group with `tcsetpgrp`, and
  takes it back with retry-on-`EINTR`.
* On every exit path (normal, `exit` builtin, fatal error, signal) the saved
  `termios` is restored once, guarded by a "restored" flag.

## 9. Content pipeline

```
packs/*.pack ──content/parse──▶ candidate records
             ──content/validate──▶ accepted records + diagnostics
             ──content/freeze──▶ immutable wl_persona_pack (session arena)
                                        │
                    narrative/persona ◀──┘  (read-only view)
```

A pack is all-or-nothing: any fatal validation error discards the whole pack
with one diagnostic line, leaving previously loaded personas untouched.
Corrupt or unknown-format packs never stop the shell from starting.

## 10. Configuration

* File: `$XDG_CONFIG_HOME/wonderland/config` (fallback
  `~/.config/wonderland/config`), `key = value` lines, `#` comments, no
  includes in v1.
* Environment overrides: `WONDERLAND_PLAIN`, `WONDERLAND_PACKS` (path list),
  `WONDERLAND_STATE` (future).
* Precedence: built-in defaults < config file < environment < command line.
* Unknown keys are reported once at startup on stderr and ignored; they never
  abort startup.

## 11. Portability

* `-std=c17 -D_POSIX_C_SOURCE=200809L`; `_DEFAULT_SOURCE` only where POSIX does
  not expose a needed symbol, each use commented.
* Linux is the target. `termios`, `fork`, `execvp`, `waitpid`, `pipe`,
  `sigaction`, `glob(3)`, `readdir` are assumed present.
* No GNU-only extensions in core logic; `__attribute__` and `__builtin_*` are
  avoided in portable files.
* Byte-oriented throughout; no locale-dependent classification in the lexer
  (use explicit ASCII tables).

## 12. Testing architecture

* `tests/unit/` — C harness, one suite per module, no external framework.
* `tests/cases/` — shell scripts executed by a differential runner against
  `sh`/`dash`/`bash`, comparing stdout, stderr and exit status separately.
* `tests/pty/` — expect-style interactive tests driven from a small C helper
  using `forkpty` (no `expect` dependency).
* `make test` runs unit + CLI + differential-if-available + isolation checks;
  `make asan` repeats the C parts under ASan/UBSan.
* Golden files store exact bytes; narrative tests assert *stdout is empty of
  narrative* by construction, and assert stderr shape only in interactive
  simulation.

## 13. Deliberate non-abstractions

Recorded so future work does not "improve" them:

* No generic visitor for the AST; `exec` and `dump` switch on node kind.
* No polymorphic builtin objects; a table of `{name, fn, flags}`.
* No event bus, no subscribers, no priorities queue — one dispatch function
  with a fixed order.
* No threading, ever, unless a measured problem demands it.
* No plugin ABI before packs are proven.
