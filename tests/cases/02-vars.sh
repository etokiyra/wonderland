# Parameter expansion and the ${...} operators.
x=hello
y=
printf '[%s]\n' "$x" "${x}" "${y:-default}" "${y-default}"
printf 'len=%s\n' "${#x}"
z=abcdef
printf '%s\n' "${z#ab}" "${z##*c}" "${z%ef}" "${z%%d*}"
unset x
printf '[%s]\n' "${x-unset}" "${x:-empty}"
n=3
printf '%s\n' "$((n * 2))"
