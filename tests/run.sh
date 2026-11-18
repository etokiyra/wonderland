#!/bin/sh
#
# Black-box tests for the bootstrap binary.
#
# Only stdout, stderr and exit status are inspected, and they are captured
# separately: a test that merges streams cannot prove stdout purity
# (AGENTS.md §7).
#
# Usage:
#   sh tests/run.sh                 # tests build/wonderland
#   WL_BIN=build/asan/wonderland sh tests/run.sh

set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 1
BIN=${WL_BIN:-$root/build/wonderland}

case $BIN in
    /*) ;;
    *) BIN=$root/$BIN ;;
esac

if [ ! -x "$BIN" ]; then
    echo "run.sh: FAIL: no executable at $BIN (build it first)" >&2
    exit 1
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/wonderland-test.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

total=0
failed=0
status=0

expect() {
    label=$1
    shift
    total=$((total + 1))
    if "$@"; then
        printf 'ok   %s\n' "$label"
    else
        printf 'FAIL %s\n' "$label"
        failed=$((failed + 1))
    fi
}

run() {
    "$BIN" "$@" >"$tmp/out" 2>"$tmp/err"
    status=$?
}

status_is() { [ "$status" -eq "$1" ]; }
stdout_empty() { [ ! -s "$tmp/out" ]; }
stderr_empty() { [ ! -s "$tmp/err" ]; }
stdout_has() { grep -q -- "$1" "$tmp/out"; }
stderr_has() { grep -q -- "$1" "$tmp/err"; }
is_zero() { [ "$1" -eq 0 ]; }
is_one() { [ "$1" -eq 1 ]; }

# --- --version -------------------------------------------------------------

run --version
expect "--version exits 0" status_is 0
expect "--version writes the version line to stdout" stdout_has '^wonderland '
expect "--version writes nothing to stderr" stderr_empty

lines=$(wc -l <"$tmp/out" | tr -d ' ')
expect "--version prints exactly one line" is_one "$lines"

# --- no operands -----------------------------------------------------------

run
expect "no operands exits 3 (unimplemented)" status_is 3
expect "no operands writes nothing to stdout" stdout_empty
expect "no operands explains itself on stderr" stderr_has "not implemented"

# --- --plain ---------------------------------------------------------------

run --plain
expect "--plain exits 3 (unimplemented)" status_is 3
expect "--plain writes nothing to stdout" stdout_empty
expect "--plain explains itself on stderr" stderr_has "not implemented"

purity=$("$BIN" --plain 2>/dev/null | wc -c | tr -d ' ')
expect "--plain emits zero bytes on stdout through a pipe" is_zero "$purity"

# --- environment -----------------------------------------------------------

WONDERLAND_PLAIN=1 "$BIN" --version >"$tmp/out" 2>"$tmp/err"
status=$?
expect "WONDERLAND_PLAIN=1 --version still exits 0" status_is 0
expect "WONDERLAND_PLAIN=1 --version still prints the version line" \
    stdout_has '^wonderland '
expect "WONDERLAND_PLAIN=1 --version keeps stderr empty" stderr_empty

env_purity=$(WONDERLAND_PLAIN=1 "$BIN" --plain 2>/dev/null | wc -c | tr -d ' ')
expect "WONDERLAND_PLAIN=1 --plain emits zero bytes on stdout" \
    is_zero "$env_purity"

# --- refusals --------------------------------------------------------------

run --bogus
expect "unknown option exits 2" status_is 2
expect "unknown option writes nothing to stdout" stdout_empty
expect "unknown option names the option" stderr_has '--bogus'

run -c 'echo hi'
expect "-c exits 3 (never executes)" status_is 3
expect "-c writes nothing to stdout" stdout_empty

run script.sh
expect "script operand exits 3 (never executes)" status_is 3
expect "script operand writes nothing to stdout" stdout_empty

run -
expect "stdin operand exits 3 (never executes)" status_is 3
expect "stdin operand writes nothing to stdout" stdout_empty

# --- summary ---------------------------------------------------------------

printf '\n%d checks, %d failed\n' "$total" "$failed"

if [ "$failed" -ne 0 ]; then
    exit 1
fi
exit 0
