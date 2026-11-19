# Redirections: create, append, input, fd duplication, pipeline to file.
printf 'one\n' > out.txt
printf 'two\n' >> out.txt
cat < out.txt
printf 'through-pipe\n' | cat > piped.txt
cat piped.txt
sh -c 'printf "out\n"; printf "err\n" >&2' > both.txt 2>&1
sort both.txt
