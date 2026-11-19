# Loops, break and continue.
for i in 1 2 3; do
    printf '%s\n' "$i"
done

i=0
while test "$i" -lt 3; do
    printf 'w%s\n' "$i"
    i=$((i + 1))
done

i=0
until test "$i" -ge 2; do
    printf 'u%s\n' "$i"
    i=$((i + 1))
done

for i in 1 2 3 4; do
    if test "$i" = 2; then continue; fi
    if test "$i" = 4; then break; fi
    printf 'f%s\n' "$i"
done
