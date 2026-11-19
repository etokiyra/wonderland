# Positional parameters and "$@".
set -- one "two three"
printf '%s\n' "$#"
printf '<%s>\n' "$@"
printf '<%s>\n' "$*"
