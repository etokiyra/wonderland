# The EXIT trap runs once, after the body.
trap 'printf "exit-trap\n"' EXIT
printf 'body\n'
