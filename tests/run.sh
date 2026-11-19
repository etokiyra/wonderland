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

script=$tmp/script.sh
printf 'echo "script:$1:$2"\nexit 4\n' >"$script"
run "$script" one two
expect "script files execute with arguments" stdout_is_line 'script:one:two'
expect "script exit status is honoured" status_is 4

run "$tmp/does-not-exist.sh"
expect "missing script exits 127" status_is 127
expect "missing script explains itself" stderr_has "No such file"

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

# --- parsing (M2) ---------------------------------------------------------

run -c 'printf "[%s]\n" "a b"'
expect "quoted space is one argument" stdout_is_line '[a b]'

run -c "printf '[%s]\n' 'a  b'"
expect "single quotes preserve spacing" stdout_is_line '[a  b]'

run -c 'printf "[%s]\n" ""'
expect "empty quoted argument survives" stdout_is_line '[]'

run -c 'printf "[%s]\n" a\ b'
expect "escaped space is one argument" stdout_is_line '[a b]'

run -c 'echo one; echo two'
expect "semicolon separates commands" stdout_is_text 'one\ntwo\n'

run -c 'false && echo no; echo yes'
expect "&& short-circuits" stdout_is_line yes

run -c 'true || echo no'
expect "|| short-circuits" status_is 0
expect "|| produces no output" stdout_empty

run -c '! true'
expect "! inverts success" status_is 1

run -c '! false'
expect "! inverts failure" status_is 0

run -c 'cd /tmp; pwd'
expect "builtins work inside a list" stdout_is_line /tmp

run -c 'echo if then fi'
expect "reserved words are ordinary arguments" stdout_is_line 'if then fi'

run -c 'echo hi # comment'
expect "comments are ignored" stdout_is_line hi

continued='echo ab\
cd'
run -c "$continued"
expect "backslash-newline continues a command" stdout_is_line abcd

run -c 'echo $HOME'
expect "parameter expansion" stdout_has "$HOME"

run -c 'X=hello; echo $X'
expect "assignments and expansion" stdout_is_line hello

run -c 'X=hello; echo "${X}there"'
expect "braced expansion" stdout_is_line hellothere

run -c 'echo $((1 + 2 * 3))'
expect "arithmetic expansion" stdout_is_line 7

run -c 'echo $(echo sub)'
expect "command substitution" stdout_is_line sub

run -c 'X="a b"; printf "[%s]\n" $X'
expect "unquoted expansion split into fields" stdout_is_text '[a]\n[b]\n'

run -c 'X="a b"; printf "[%s]\n" "$X"'
expect "quoted expansion stays one field" stdout_is_line '[a b]'

run -c 'X=abcdef; echo ${X#ab} ${X%%d*}'
expect "pattern trimming" stdout_is_line 'cdef abc'

run -c 'X=; printf "[%s]\n" ${X:-a b}'
expect "unquoted operand splits" stdout_is_text '[a]\n[b]\n'

run -c 'X=; printf "[%s]\n" ${X:-"a b"}'
expect "quoted operand stays one field" stdout_is_line '[a b]'

run -c 'unset Y; echo ${Y:=assigned}; echo $Y'
expect ":= assigns the default" stdout_is_text 'assigned\nassigned\n'

run -c 'echo ${MISSING:?custom message}'
expect ":? fails the command" status_is 1
expect ":? reports its message" stderr_has "custom message"

run -c "> $tmp/emptyfile; test -f $tmp/emptyfile"
expect "redirection without a command" status_is 0

run -c 'X=1; unset X; echo "[$X]"'
expect "unset removes the variable" stdout_is_line '[]'

run -c "export WL_T=42; sh -c 'echo \$WL_T'"
expect "exported variables reach children" stdout_is_line 42

run -c "X=one sh -c 'echo \$X'"
expect "prefix assignment reaches the child environment" stdout_is_line one

# --- redirections and pipelines -------------------------------------------

run -c "echo hi > $tmp/outfile; cat $tmp/outfile"
expect "output redirection" stdout_is_line hi

run -c "echo one > $tmp/app; echo two >> $tmp/app; cat $tmp/app"
expect "append redirection" stdout_is_text 'one\ntwo\n'

run -c "printf 'a\nb\n' > $tmp/infile; wc -l < $tmp/infile"
expect "input redirection" stdout_is_line 2

run -c 'echo out 2>&1'
expect "2>&1 duplicates stdout" stdout_is_line out
expect "2>&1 leaves stderr empty" stderr_empty

run -c 'echo hi | tr a-z A-Z'
expect "pipeline output" stdout_is_line HI

run -c 'false | true; echo $?'
expect "pipeline status is the last stage" stdout_is_line 0

run -c 'true | false; echo $?'
expect "pipeline status reflects failure" stdout_is_line 1

run -c 'echo one | cat | wc -l'
expect "three-stage pipeline" stdout_is_line 1

heredoc_input='cat <<EOF
hi $HOME
EOF'
run -c "$heredoc_input"
expect "here-document body expands" stdout_has "$HOME"

heredoc_quoted='cat <<'"'"'EOF'"'"'
no $HOME here
EOF'
run -c "$heredoc_quoted"
expect "quoted here-document delimiter is literal" stdout_is_line 'no $HOME here'

touch "$tmp/glob-a.txt" "$tmp/glob-b.txt"
run -c "echo $tmp/glob-*.txt"
expect "pathname expansion" stdout_is_line "$tmp/glob-a.txt $tmp/glob-b.txt"

run -c "echo $tmp/nope-*.txt"
expect "unmatched glob stays literal" stdout_is_line "$tmp/nope-*.txt"

run -c 'if true; then echo y; fi'
expect "if executes its then branch" stdout_is_line y

run -c 'if false; then echo y; elif true; then echo elif; else echo n; fi'
expect "elif branch" stdout_is_line elif

run -c 'for i in a b c; do echo $i; done'
expect "for loop" stdout_is_text 'a\nb\nc\n'

run -c 'i=0; while test $i -lt 2; do echo $i; i=$((i+1)); done'
expect "while loop" stdout_is_text '0\n1\n'

run -c 'i=0; until test $i -ge 2; do echo $i; i=$((i+1)); done'
expect "until loop" stdout_is_text '0\n1\n'

run -c 'for i in 1 2 3; do if test $i = 2; then continue; fi; echo $i; done'
expect "continue skips an iteration" stdout_is_text '1\n3\n'

run -c 'for i in 1 2 3; do test $i = 2 && break; echo $i; done'
expect "break leaves the loop" stdout_is_line 1

run -c 'case hello in h*) echo match;; *) echo no;; esac'
expect "case matches a pattern" stdout_is_line match

run -c 'case x in a|b) echo ab;; esac; echo after'
expect "case with no match continues" stdout_is_line after

run -c 'f() { echo "f:$1"; }; f arg'
expect "function call with an argument" stdout_is_line 'f:arg'

run -c 'f() { return 7; }; f; echo $?'
expect "return sets the function status" stdout_is_line 7

run -c 'set -- a b c; shift; echo "$# $1"'
expect "shift removes positional parameters" stdout_is_line '2 b'

run -c 'set -- one "two three"; printf "[%s]\n" "$@"'
expect "quoted $@ expands to separate fields" stdout_is_text '[one]\n[two three]\n'

run_stdin 'a b c\n' -c 'read x y; echo "$x|$y"'
expect "read splits input into variables" stdout_is_line 'a|b c'

run_stdin 'hello world\n' -c 'read; echo "[$REPLY]"'
expect "read without names sets REPLY" stdout_is_line '[hello world]'

run_stdin '' -c 'read x; echo "status=$?"'
expect "read returns 1 at end of file" stdout_is_line 'status=1'

cat >"$tmp/ansi.sh" <<'CASE'
printf '%s\n' $'a\tb' $'\x41'
CASE
run "$tmp/ansi.sh"
expect "ANSI-C quoting decodes escapes" stdout_is_text 'a\tb\nA\n'

run -c 'sleep 0.1 & pid=$!; test "$pid" -gt 0 && echo pid-ok; wait'
expect "the recent background pid is available" stdout_is_line pid-ok

run -c 'jobs'
expect "jobs with an empty table is silent" stdout_empty

run -c 'sleep 0.1 & wait; echo done'
expect "background job and wait" stdout_is_text 'done\n'

run -c 'sleep 0.5 & jobs; wait'
expect "jobs lists a background job" stdout_has "Running"
expect "jobs shows the command" stdout_has "sleep 0.5"

run -c 'wait %9'
expect "wait on an unknown job fails" status_is 127

run -c 'fg'
expect "fg with no jobs fails" status_is 1

run -c 'bg'
expect "bg with no jobs fails" status_is 1

run -c 'X=outer; (X=inner; echo $X); echo $X'
expect "subshell changes do not leak" stdout_is_text 'inner\nouter\n'

run -c '{ X=group; }; echo $X'
expect "brace group changes persist" stdout_is_line group

run -c 'trap "echo bye" EXIT; echo hi'
expect "EXIT trap runs at shutdown" stdout_is_text 'hi\nbye\n'

run -c "trap 'echo term-trap' TERM; sh -c 'kill -TERM \$PPID'; echo after"
expect "TERM trap runs" stdout_is_text 'term-trap\nafter\n'

run -c "trap 'echo int-trap' INT; sh -c 'kill -INT \$PPID'; echo after"
expect "INT trap runs" stdout_is_text 'int-trap\nafter\n'

run -c 'trap "echo t" EXIT; exit 5'
expect "EXIT trap does not change the exit status" status_is 5
expect "EXIT trap still runs" stdout_is_line t

run -c 'set -n; echo suppressed'
expect "set -n suppresses execution" stdout_empty

run -c 'f() { echo hi; }; f | cat'
expect "functions work in pipelines" stdout_is_line hi

run -c '{ echo grouped; } | cat'
expect "groups work in pipelines" stdout_is_line grouped

run -c 'set -e; false; echo unreachable'
expect "set -e stops execution" status_is 1
expect "set -e produces no further output" stdout_empty

run -c 'set -u; echo $MISSING'
expect "set -u rejects unset variables" status_is 1
expect "set -u names the variable" stderr_has "MISSING"

run -c 'set -x; echo traced' 2>"$tmp/err"
expect "set -x traces commands" stderr_has "+ echo traced"

run -c 'echo "unterminated'
expect "unterminated quote is a syntax error" status_is 2
expect "unterminated quote names the problem" stderr_has "unterminated"

# --- limits ---------------------------------------------------------------

awk 'BEGIN { for (i = 0; i < 1100000; i++) printf "a"; printf "\n" }' |
    "$BIN" >"$tmp/out" 2>"$tmp/err"
status=$?
expect "over-large stdin is refused" status_is 2
expect "over-large stdin explains itself" stderr_has "too large"

many_words=$(awk 'BEGIN { for (i = 0; i < 300; i++) printf "zzz "; }')
run -c "$many_words"
expect "large argument lists reach execution" status_is 127
expect "large argument lists name the missing command" stderr_has "zzz: command not found"

# --- summary ---------------------------------------------------------------

printf '\n%d checks, %d failed\n' "$total" "$failed"

if [ "$failed" -ne 0 ]; then
    exit 1
fi
exit 0
