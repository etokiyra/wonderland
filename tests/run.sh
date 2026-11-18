#!/bin/sh
#
# Black-box tests for the M1 shell.
#
# stdout, stderr and the exit status are captured separately: a test that
# merges streams cannot prove stdout purity (AGENTS.md §7).
#
# Usage:
#   sh tests/run.sh
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
    "$BIN" "$@" </dev/null >"$tmp/out" 2>"$tmp/err"
    status=$?
}

run_stdin() {
    input=$1
    shift
    printf '%b' "$input" | "$BIN" "$@" >"$tmp/out" 2>"$tmp/err"
    status=$?
}

status_is() { [ "$status" -eq "$1" ]; }
stdout_empty() { [ ! -s "$tmp/out" ]; }
stderr_empty() { [ ! -s "$tmp/err" ]; }
stdout_has() { grep -q -- "$1" "$tmp/out"; }
stderr_has() { grep -q -- "$1" "$tmp/err"; }

stdout_is_line() {
    printf '%s\n' "$1" >"$tmp/expected"
    cmp -s "$tmp/out" "$tmp/expected"
}

stdout_is_text() {
    printf '%b' "$1" >"$tmp/expected"
    cmp -s "$tmp/out" "$tmp/expected"
}

is_zero() { [ "$1" -eq 0 ]; }
is_one() { [ "$1" -eq 1 ]; }
bytes_are() { [ "$1" -eq "$2" ]; }

# --- version and usage -----------------------------------------------------

run --version
expect "--version exits 0" status_is 0
expect "--version writes the version line to stdout" stdout_has '^wonderland '
expect "--version writes nothing to stderr" stderr_empty

lines=$(wc -l <"$tmp/out" | tr -d ' ')
expect "--version prints exactly one line" is_one "$lines"

run --bogus
expect "unknown option exits 2" status_is 2
expect "unknown option writes nothing to stdout" stdout_empty
expect "unknown option names the option" stderr_has '--bogus'

run script.sh
expect "script operand exits 3 (not implemented until M3)" status_is 3
expect "script operand writes nothing to stdout" stdout_empty

# --- -c --------------------------------------------------------------------

run -c 'echo hello'
expect "-c runs a command" status_is 0
expect "-c writes command output to stdout" stdout_is_line hello
expect "-c writes nothing to stderr on success" stderr_empty

run -c 'false'
expect "-c propagates a failing status" status_is 1
expect "-c failing command writes nothing to stdout" stdout_empty

run -c 'exit 5'
expect "-c exit 5" status_is 5

run -c 'exit abc'
expect "-c exit with a non-numeric argument exits 2" status_is 2
expect "-c exit with a non-numeric argument explains itself" stderr_has "numeric"

run -c 'exit 1 2'
expect "-c exit with too many arguments does not exit silently" status_is 1

two_lines='echo one
echo two'
run -c "$two_lines"
expect "-c runs newline-separated lines" status_is 0
expect "-c output is in order" stdout_is_text 'one\ntwo\n'

run -c 'false
true'
expect "-c status is the last line's status" status_is 0

run -c 'true
false'
expect "-c status reflects the final failure" status_is 1

run -c '   '
expect "-c whitespace runs nothing and succeeds" status_is 0
expect "-c whitespace writes nothing to stdout" stdout_empty

# --- external commands and PATH -------------------------------------------

run -c 'nonexistent-wonderland-command'
expect "missing command exits 127" status_is 127
expect "missing command writes nothing to stdout" stdout_empty
expect "missing command explains itself" stderr_has "command not found"

run -c '/etc/passwd'
expect "non-executable file exits 126" status_is 126
expect "non-executable file explains itself" stderr_has "Permission denied"

# --- cd --------------------------------------------------------------------

run -c 'cd /tmp'
expect "cd succeeds" status_is 0
expect "cd writes nothing to stdout" stdout_empty

run -c 'cd /definitely/not/here'
expect "cd failure exits 1" status_is 1
expect "cd failure explains itself" stderr_has "cd:"

# --- stdin (non-interactive) ----------------------------------------------

run_stdin 'echo hi\n'
expect "stdin commands run" status_is 0
expect "stdin command output" stdout_is_line hi

run_stdin 'false\n'
expect "stdin status is the last command's" status_is 1

run_stdin 'exit 3\n'
expect "stdin exit status" status_is 3

run_stdin ''
expect "empty stdin exits 0" status_is 0
expect "empty stdin writes nothing" stdout_empty

run_stdin 'echo via-dash\n' -
expect "'-' reads stdin" status_is 0
expect "'-' output" stdout_is_line via-dash

run_stdin 'cd /tmp\npwd\n'
expect "state persists across stdin lines" status_is 0
expect "cwd changed for the next line" stdout_is_line /tmp

# --- signals --------------------------------------------------------------

signal_script=$tmp/die.sh
printf '#!/bin/sh\nkill -TERM $$\n' >"$signal_script"
chmod +x "$signal_script"
run -c "$signal_script"
expect "signal death maps to 128+signal" status_is 143
expect "signal death writes nothing to stdout" stdout_empty

# --- stdout purity --------------------------------------------------------

purity=$("$BIN" -c 'echo hi' 2>/dev/null | wc -c | tr -d ' ')
expect "-c emits exactly the command's bytes on stdout" bytes_are "$purity" 3

"$BIN" --plain -c 'echo hi' >"$tmp/out" 2>"$tmp/err"
status=$?
expect "--plain still executes commands" status_is 0
expect "--plain does not change command output" stdout_is_line hi

WONDERLAND_PLAIN=1 "$BIN" -c 'echo env-hi' >"$tmp/out" 2>"$tmp/err"
status=$?
expect "WONDERLAND_PLAIN=1 still executes commands" status_is 0
expect "WONDERLAND_PLAIN=1 output" stdout_is_line env-hi

# --- limits ---------------------------------------------------------------

long_command=$(awk 'BEGIN { for (i = 0; i < 5000; i++) printf "a" }')
run -c "$long_command"
expect "over-long command line is refused" status_is 2
expect "over-long command line explains itself" stderr_has "too long"

many_words=$(awk 'BEGIN { for (i = 0; i < 300; i++) printf "w "; }')
run -c "$many_words"
expect "too many arguments are refused" status_is 2
expect "too many arguments explain themselves" stderr_has "too many"

# --- summary ---------------------------------------------------------------

printf '\n%d checks, %d failed\n' "$total" "$failed"

if [ "$failed" -ne 0 ]; then
    exit 1
fi
exit 0
