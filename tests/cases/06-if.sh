# if / elif / else.
if true; then printf 'yes\n'; else printf 'no\n'; fi
if false; then
    printf 'a\n'
elif true; then
    printf 'b\n'
else
    printf 'c\n'
fi
if false; then :; fi
echo $?
