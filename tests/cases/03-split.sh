# Field splitting: only unquoted expansion results split.
v="a b  c"
printf '<%s>\n' $v
printf '<%s>\n' "$v"
IFS=:
w="x:y:z"
printf '<%s>\n' $w
unset IFS
p="one two"
printf '<%s>\n' $p
