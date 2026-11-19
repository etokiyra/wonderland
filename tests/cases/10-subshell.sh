# Subshell isolation and brace groups.
x=outer
(
    x=inner
    printf 'in:%s\n' "$x"
)
printf 'out:%s\n' "$x"

{
    y=group
}
printf 'group:%s\n' "$y"

(
    printf 'subshell\n'
)
