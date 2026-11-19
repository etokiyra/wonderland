# Wonderland — Compatibility

Status: no shell behaviour is implemented. This document defines what will be
tested before anything is claimed, so that "compatible" is a fact about the
test suite and not a mood.

## 1. Position

1. **POSIX sh first.** The reference is POSIX.1-2017 XCU, "Shell Command
   Language" (`sh`). Where the standard is silent or contradictory, dash's
   behaviour is the tie-breaker, because dash is the strictest widely deployed
   `sh`.
2. **Selected Bash extensions second**, after the POSIX surface is stable and
   tested. Not before.
3. **No compatibility theatre.** Wonderland is not bash, does not set
   `BASH_VERSION`, and does not pretend. `#!/bin/bash` scripts are executed by
   the kernel against the real bash binary and never reach Wonderland's parser.
4. **Claims are per-feature and test-gated.** `docs/COMPATIBILITY.md` is the
   only place a feature may be called supported, and each entry names the test
   file that proves it.

## 2. Environment facts (re-measured 2026-11-19)

| Tool | Found | Consequence |
|------|-------|-------------|
| `bash` | 5.3.20(1) | differential reference available |
| `sh` → `/bin/sh` | bash 5.3.20 (bash in POSIX mode is **not** guaranteed to match dash) | cannot stand in for dash |
| `dash` | 0.5.13.5, installed | the conservative POSIX reference is available; the differential harness is still to be written |
| `valgrind` | 3.25.1 | `make memcheck` runs the unit suite under it |
| `cppcheck` | 2.22.0 | `make lint` runs it over `src/` |
| `ksh`, `mksh`, `zsh`, `yash`, `busybox sh` | not installed | out of scope for the local matrix |

The dash gap noted in the first audit is closed at the tool level; the
differential runner itself is still a design (`docs/ROADMAP.md` M2–M3), so no
compatibility claim in this document rests on it yet. `sh` is never treated as
dash.

## 3. Support matrix

Legend: **planned** = in a milestone, **tested** = has a passing differential
case, **no** = out of scope.

### 3.0 Implemented at M1–M3a (2026-11-19), with its own tests

This is deliberately *not* called supported in the POSIX sense: there is no
differential case yet, so these are regression tests against our own
specification, not against another shell.

| Behaviour | Evidence |
|-----------|----------|
| Simple commands with whitespace-separated words | `tests/run.sh`, `tests/unit/test_ast.c` |
| Quoting: `'...'`, `"..."`, backslash, `""` surviving as an argument | `tests/unit/test_ast.c`, `tests/run.sh` |
| `;` lists, `&&`, `\|\|`, `!` | `tests/unit/test_ast.c`, `tests/run.sh` |
| Comments and `\<newline>` continuation | `tests/unit/test_ast.c`, `tests/run.sh`, pty |
| Parameter expansion: `$x`, `${x}`, `:-`, `:=`, `:?`, `:+`, `#`, `##`, `%`, `%%`, `${#x}` | `tests/run.sh`, `tests/unit/test_arith.c` |
| Command substitution `$(...)` | `tests/run.sh` |
| Arithmetic `$((...))`: `+ - * / %`, parentheses, variables | `tests/unit/test_arith.c`, `tests/run.sh` |
| Tilde expansion | `tests/run.sh` (manual `~`) |
| Quote removal, `IFS` splitting, globbing with literal no-match fallback | `tests/run.sh` |
| Variables, `export`, `unset`, prefix and standalone assignments | `tests/unit/test_var.c`, `tests/run.sh` |
| Redirections: `< > >> <> >\| <& >& \|`, here-documents, here-strings, left-to-right order | `tests/run.sh`, pty |
| Pipelines (2+ stages, status from the last, terminal ownership) | `tests/run.sh`, pty |
| `$PATH` search, direct paths, 127 vs 126 | `tests/unit/test_path.c`, `tests/run.sh` |
| Exit status propagation, 128+N for signals | `tests/run.sh` |
| `cd` (HOME, `-`, PWD/OLDPWD) and `exit` (0–255) | `tests/unit/test_builtins.c`, `tests/run.sh` |
| `-c`, stdin, `-` | `tests/run.sh` |
| Starship-style left/right prompt, `$?`/git/job/duration display, HOME abbreviation, `classic`/ASCII/`NO_COLOR` fallbacks | `tests/unit/test_prompt.c`, `tests/pty/pty_test.c` |
| Ctrl-C at the prompt; unchanged termios | `tests/pty/pty_test.c` |
| Ctrl-Z is a no-op, does not wedge the shell (D-0051) | `tests/pty/pty_test.c` |
| `if`/`elif`/`else`, `while`, `until`, `for`, `case` | `tests/cases/06–08`, `tests/run.sh`, `tests/diff.sh` |
| Functions, `return`, recursion, positional parameters, `shift`, `set --` | `tests/cases/09`, `tests/cases/13`, `tests/run.sh` |
| `{ }` groups and `( )` subshells (state isolation) | `tests/cases/10`, `tests/run.sh`, `tests/diff.sh` |
| `break`, `continue`, including `N` levels | `tests/cases/07`, `tests/run.sh` |
| `trap` on `EXIT`, `INT`, `TERM` | `tests/cases/14`, `tests/run.sh` |
| `set -e`, `-u`, `-x`, `-n` (documented approximations) | `tests/run.sh`, D-0074 |
| Script files with arguments; `-c CMD name args` | `tests/run.sh` |
| Differential agreement with dash and bash | `tests/diff.sh`, 14 cases |
| Syntax diagnostics with line, column and caret | `tests/unit/test_ast.c`, `tests/run.sh` |
| Background `&` for any statement, `$!`, `wait` | `tests/run.sh`, pty |
| `jobs`, `fg`, `bg`, `%n`/`%%`/`%-`, Ctrl-Z suspend/resume | `tests/run.sh`, pty |
| Raw-mode line editor: arrows, Home/End/Delete, Ctrl-A/E/B/F/K/U/W/L/C/D, history recall | `tests/pty/pty_test.c` |
| History: XDG file, 0600, bounded, duplicate suppression | `tests/unit/test_history.c` |
| Tab completion: `$PATH` commands and paths (common prefix) | `tests/pty/pty_test.c` |
| `WONDERLAND_PROMPT`, SIGWINCH redraw | pty session; documented limits |
| `$'...'` ANSI-C quoting | `tests/run.sh` |
| `read [-r] name...`, `REPLY`, EOF status | `tests/run.sh` |
| Narrative layer is outside the compatibility surface: five pack-loaded personas, interactive-only, stderr, plain-gated; mechanics (cycle, `rest`/`respawn`, death screen, mirror, `go-to-alice`) never change statuses | `tests/pty/pty_test.c` (plain, narrative, cycle/job, `--plain` sessions), `tests/unit/test_pack.c`, `test_narrative.c`, `test_state.c` |
| Non-whitespace `IFS` exactness, `"pre$@"` adjacency, `kill`/`disown`, async job notifications, UTF-8 cursor movement, completion listing | **not implemented/approximate** (D-0068, D-0076, D-0082, D-0089) |

### 3.1 POSIX surface

| Feature | Status | Milestone |
|---------|--------|-----------|
| Simple commands, `PATH` lookup, `fork`/`exec` | planned | M1 |
| Exit status, `$?`, `exit`, `cd` | planned | M1 |
| Single/double quotes, backslash | planned | M2 |
| Parameter expansion (`$x`, `${x}`, `${x:-w}`, `${#x}`, `${x%p}`, `${x#p}`) | planned | M2/M3 |
| Field splitting with `IFS` | planned | M3 |
| Pathname expansion (globbing) with no-match behaviour | planned | M3 |
| Command substitution `$(...)`, legacy `` `...` `` | planned | M3 |
| Arithmetic expansion `$((...))` | planned | M3 |
| Pipelines | planned | M3 |
| Redirections `< > >> <> >\| <& >&` | planned | M3 |
| Here-documents `<<`, `<<-` | planned | M2 (parse), M3 (exec) |
| `&&`, `\|\|`, `;`, `&` | planned | M3 |
| Subshells, brace groups | planned | M3 |
| `if`, `while`, `until`, `for`, `case` | planned | M2/M3 |
| Functions, positional parameters, `return` | planned | M3 |
| `set -e/-u/-x/-n`, `trap` | planned | M3/M4 |
| Job control (`jobs`, `fg`, `bg`, `%1`, Ctrl-Z) | planned | M4 |
| `command`, `exec`, `readonly`, `export`, `unset`, `read`, `shift` | planned | M3/M5 |
| `printf` builtin | planned | M5 |
| `getopts`, `umask`, `times`, `ulimit`, `wait` | planned | M8 |
| Signal/trap semantics (`EXIT`, `INT`, `TERM`) | planned | M4 |

### 3.2 Bash extensions (explicitly deferred)

| Extension | Decision |
|-----------|----------|
| `[[ ... ]]` | after M8, if it does not distort the parser |
| Arrays, `arr=(a b)`, `${arr[@]}` | after M8; largest grammar impact, evaluated last |
| `${var//a/b}`, `${var:offset:len}`, `${!var}`, `${var^^}` | M8, expander-local |
| Process substitution `<(cmd)`, `>(cmd)` | after M8 |
| `<<<` here-strings | M8, trivial once redirections exist |
| `set -o pipefail`, `shopt` | M8, opt-in only |
| Brace expansion `{a,b}` | after M8 |
| `local`, `declare`, `typeset` | M8 |
| `$'...'`, `$"..."` | M5 (lexer), low risk |
| `;&`, `;;&` in `case` | not planned |
| Extglob, `coproc`, `select`, `time` | not planned |
| `BASH_*` variables, `BASH_VERSION` | never faked |

### 3.3 Out of scope

csh/rc syntax, Windows, network shells, plugin ABIs, `set -o vi` (until the
editor is mature), programmable completion, and any feature that requires
claiming to be bash.

## 4. How "supported" is defined

A feature is **supported** only when all of the following hold:

1. It has at least one case in `tests/cases/<area>/`.
2. The case is run under Wonderland, `dash` (when available) and `bash`, and
   compares:
   * stdout bytes,
   * stderr bytes *after* normalising the shell's own diagnostics? **No** —
     stderr is compared separately and only where the shells are expected to
     agree; shell-specific diagnostics are excluded from byte comparison and
     checked for presence/status instead.
   * exit status,
   * for job-control cases: process-group state read through `ps`, not text
     scraping.
3. Any intentional divergence is listed in §6 with a reason and a test.

## 5. Differential test harness (design)

Status: implemented as `tests/diff.sh` for the M3b surface. It runs every
`tests/cases/*.sh` under Wonderland, dash and bash in a clean environment and
requires stdout and status to agree with both; stderr is excluded because
diagnostic wording is not standardised. It skips visibly when either reference
shell is missing, and `WL_REQUIRE_DIFF=1` turns that skip into a failure. The
cases currently cover 14 areas (words, variables, splitting, redirections,
pipes, if, loops, case, functions, subshells, status, heredocs, parameters,
traps). Features not covered by a case are not "supported", only implemented.

```
tests/cases/<area>/<name>.sh     the script under test
tests/cases/<area>/<name>.out    expected stdout (golden, generated from dash
                                 or bash and reviewed by a human)
tests/cases/<area>/<name>.status expected exit status
tests/cases/<area>/<name>.env    optional key=value environment
```

Runner (`tests/run.sh`):

* runs each case with `env -i PATH=/usr/bin:/bin HOME=$tmp LC_ALL=C` so
  results do not depend on the developer's environment;
* compares stdout and status exactly;
* compares stderr only when the case opts in (`WL_COMPARE_STDERR=1`), because
  diagnostic wording is not standardised;
* re-runs the same script under `dash` and `bash` when present and reports
  divergence as `KNOWN-DIVERGENCE` (listed in §6) or `FAIL`;
* treats a missing `dash` as `SKIP` with a loud summary line, and as `FAIL`
  when `WL_REQUIRE_DASH=1`;
* exits non-zero if any case fails or if the skip count exceeds a configured
  budget.

Exit statuses: `0` all pass, `2` parse/usage error, `1` test failures
(the runner follows the same shell convention it tests).

Golden files are generated once from the reference shell and reviewed; they
are never regenerated automatically inside `make test`.

## 6. Intentional divergences

None yet. When one appears it is recorded here with: the behaviour, the
reference shells' behaviour, the reason, and the test that pins it. Planned
candidates:

* `echo` will be the POSIX-ish builtin with no `-e`/`-E` extensions in v1;
  scripts relying on bash's `echo -e` are directed to `printf`.
* Ctrl-Z does not suspend a command until M4; children ignore SIGTSTP
  (D-0051). The shell never hangs waiting for a stopped child.
* Non-interactive children share the shell's process group, so Ctrl-C kills
  the command and the shell together when `-c` runs on a terminal (D-0051).
* `cd` defaults to logical (`-L`) with `-P` available, matching POSIX.
* `set -e` will follow the POSIX text and the dash behaviour, which surprises
  people; the divergence table will state which.
* Diagnostics text differs from both shells by design; only presence and
  status are tested.

## 7. What is never claimed

* "POSIX compliant" — until a conformance suite (e.g. a port of the
  shell test suites used by dash/bash) runs clean.
* "Bash compatible" — in any form, including in `--version`, README badges, or
  documentation headlines.
* "Drop-in replacement for bash" — even after arrays work.
* "Tested on macOS/BSD" — the target is Linux.

`--version` prints:

```
wonderland 0.6.0 (M7; Alice's Library)
```

and nothing else. Feature claims live in the docs, keyed to tests.

## 8. Script execution rules

* `wonderland script.sh` parses the file; a leading `#!` line is a comment, as
  POSIX requires.
* `#!/bin/bash` execution via `./script.sh` never involves Wonderland: the
  kernel runs bash. Wonderland does not intercept shebangs.
* `wonderland -c 'cmd'` and file execution are always plain (amendment 2) and
  never touch the tty for presentation.
* Exit status of a script is the status of its last command, or the argument
  to `exit`; a syntax error is status 2.

## 9. Dogfooding criteria before installation

The shell is not added to `/etc/shells` and not set via `chsh` until
(amendment 4):

1. It has been the interactive shell for at least four weeks of daily use,
   started manually from bash in a terminal.
2. The differential suite passes for the whole POSIX surface in §3.1.
3. No known data-loss bug (history file, checkpoint file, redirect clobbering).
4. Ctrl-C/Ctrl-Z/`fg` have been exercised under load without orphaning
   processes or leaving the terminal in raw mode.
5. A documented recovery path exists for a wedged terminal.

Until then, the installation instructions (M8) explicitly say: run it *beside*
bash, not instead of it.

## 10. Known compatibility pitfalls to test first

These are the ones that historically break shells; each gets a case early:

1. `IFS` defaults, IFS unset, IFS empty, IFS containing whitespace vs not.
2. Glob with no match: the pattern is passed literally (not an error).
3. `"$@"` with zero arguments vs one empty argument.
4. Redirection order (`>f 2>&1` vs `2>&1 >f`) and `noclobber`.
5. Assignment prefix environment (`a=1 cmd` vs `a=1; cmd`), and `a=1 b=$a`.
6. `cd` with `CDPATH`, `cd -`, `cd` with no arguments (`$HOME` unset).
7. `set -e` inside `if`, `&&`, subshells, and functions.
8. `trap` on `EXIT` running exactly once and preserving the original status.
9. `wait` on a specific pid vs all, with a trapped signal.
10. Pipelines: each stage its own process, `$?` is the last stage, `SIGPIPE`
    handling, `yes | head` not hanging.
11. Builtin vs external precedence (`command`, `type`, hashing).
12. `exec` replacing the shell, and `exec` with redirections only.
