# Functions, return, positional parameters and shift.
greet() {
    printf 'hello %s\n' "$1"
}
greet world

status() {
    return 0
}
status
echo $?

outer=1
setx() {
    outer=2
}
setx
echo "$outer"

args() {
    printf '%s %s\n' "$#" "$1"
}
args a b

set -- x y z
shift 2
printf '%s %s\n' "$#" "$1"
