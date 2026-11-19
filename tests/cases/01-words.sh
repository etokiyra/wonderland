# Words, quoting and escapes.
printf '[%s]\n' "a b" 'c  d' e\ f "" x""y
printf '<%s>\n' "quoted $((1 + 2))"
printf '%s\n' 'single' "double" plain
