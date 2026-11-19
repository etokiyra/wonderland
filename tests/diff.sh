#!/bin/sh
#
# Differential tests: run every case under Wonderland, dash and bash, and
# require stdout and exit status to agree. stderr is not compared because
# diagnostic wording is not standardised.
#
# Skips (visibly) when dash or bash is missing. Set WL_REQUIRE_DIFF=1 to turn a
# skip into a failure — used when the environment is supposed to have both.

set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 1
BIN=${WL_BIN:-$root/build/wonderland}

case $BIN in
    /*) ;;
    *) BIN=$root/$BIN ;;
esac

if [ ! -x "$BIN" ]; then
    echo "diff: FAIL: $BIN is not executable" >&2
    exit 1
fi

for tool in dash bash; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "diff: SKIP: $tool is not installed"
        if [ "${WL_REQUIRE_DIFF:-0}" = "1" ]; then
            exit 1
        fi
        exit 0
    fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/wonderland-diff.XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT HUP INT TERM

total=0
failed=0

for case_file in "$root"/tests/cases/*.sh; do
    [ -f "$case_file" ] || continue
    name=$(basename "$case_file")
    total=$((total + 1))

    for shell in wonderland dash bash; do
        case_dir="$work/$name/$shell"
        mkdir -p "$case_dir" || exit 1

        case $shell in
            wonderland) runner=$BIN ;;
            dash) runner=$(command -v dash) ;;
            bash) runner=$(command -v bash) ;;
        esac

        (
            cd "$case_dir" || exit 1
            env -i PATH=/usr/bin:/bin HOME="$case_dir" LC_ALL=C \
                "$runner" "$case_file" >stdout 2>stderr
            echo $? >status
        )
    done

    base="$work/$name"
    if ! cmp -s "$base/dash/stdout" "$base/wonderland/stdout" ||
        ! cmp -s "$base/dash/status" "$base/wonderland/status"; then
        echo "FAIL $name (wonderland vs dash)"
        echo "--- stdout diff (dash vs wonderland) ---"
        diff -u "$base/dash/stdout" "$base/wonderland/stdout" || true
        echo "--- status: dash=$(cat "$base/dash/status") wonderland=$(cat "$base/wonderland/status")"
        failed=$((failed + 1))
        continue
    fi

    if ! cmp -s "$base/bash/stdout" "$base/wonderland/stdout" ||
        ! cmp -s "$base/bash/status" "$base/wonderland/status"; then
        echo "FAIL $name (wonderland vs bash)"
        echo "--- stdout diff (bash vs wonderland) ---"
        diff -u "$base/bash/stdout" "$base/wonderland/stdout" || true
        echo "--- status: bash=$(cat "$base/bash/status") wonderland=$(cat "$base/wonderland/status")"
        failed=$((failed + 1))
        continue
    fi

    printf 'ok   %s\n' "$name"
done

printf '\n%d cases, %d failed\n' "$total" "$failed"
[ "$failed" -eq 0 ] || exit 1
exit 0
