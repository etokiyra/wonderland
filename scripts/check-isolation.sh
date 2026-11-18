#!/bin/sh
#
# Structural check for the narrative isolation boundary.
#
# This is one of four mechanisms (docs/ARCHITECTURE.md §3.1). It is a lint, not
# a proof: the link probe in the Makefile is the proof. Both run in `make test`.
#
# Exit status: 0 clean, 1 violation or nothing to check.

set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 1
narrative="$root/src/narrative"

if [ ! -d "$narrative" ]; then
    echo "isolation: FAIL: $narrative does not exist" >&2
    exit 1
fi

files=$(find "$narrative" -type f \( -name '*.c' -o -name '*.h' \) | sort)
if [ -z "$files" ]; then
    echo "isolation: FAIL: no narrative sources found (refusing a vacuous pass)" >&2
    exit 1
fi

checked=$(printf '%s\n' "$files" | wc -l | tr -d ' ')
failed=0

report() {
    failed=1
    echo "isolation: FAIL: $*" >&2
}

# 1. No shell-internal includes.
if grep -InE \
    '#[[:space:]]*include[[:space:]]*"(core|exec|jobs|builtins|parse|ast|expand|line|term|content)/' \
    $files >&2; then
    report "narrative includes shell-internal headers (listed above)"
fi

# 2. No process, environment, terminal, or termination calls.
#    The prefix class keeps matches to whole identifiers: wl_narrative_react(
#    must not be mistaken for a call to exec*.
forbidden_call='(^|[^[:alnum:]_])(fork|vfork|execv|execve|execvp|execl|execle|execlp|execlpe|system|popen|setenv|putenv|unsetenv|chdir|fchdir|dup2|kill|raise|tcsetpgrp|tcgetpgrp|exit|_exit)[[:space:]]*\('

for file in $files; do
    if grep -InE "$forbidden_call" "$file" >&2; then
        report "forbidden call in $file"
    fi
done

# 3. Narrative performs no I/O of its own: it returns text, the shell writes
#    it (docs/DECISIONS.md D-0015).
forbidden_io='(^|[^[:alnum:]_])(printf|fprintf|vprintf|dprintf|puts|putchar|fwrite|fputs|fputc|write|writev)[[:space:]]*\('

for file in $files; do
    if grep -InE "$forbidden_io" "$file" >&2; then
        report "narrative performs output in $file"
    fi
done

forbidden_stream='(^|[^[:alnum:]_])(stdout|stderr|fileno|isatty)([^[:alnum:]_]|$)'

for file in $files; do
    if grep -InE "$forbidden_stream" "$file" >&2; then
        report "narrative touches a standard stream in $file"
    fi
done

# 4. The dependency must not be inverted: shell code may include the narrative
#    ABI headers, never narrative internals.
allowed_includes='"narrative/(narrative|persona|event)\.h"'

for dir in "$root/src/exec" "$root/src/builtins"; do
    [ -d "$dir" ] || continue
    bad=$(grep -rhoE '#[[:space:]]*include[[:space:]]*"narrative/[^"]+"' "$dir" 2>/dev/null |
        grep -vE "$allowed_includes" || true)
    if [ -n "$bad" ]; then
        report "shell code includes narrative internals: $bad"
    fi
done

if [ "$failed" -ne 0 ]; then
    exit 1
fi

echo "isolation: ok ($checked narrative files checked)"
exit 0
