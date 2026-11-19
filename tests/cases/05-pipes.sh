# Pipelines and their exit status.
printf 'a\nb\nc\n' | wc -l
printf 'x\ny\n' | sort | tr a-z A-Z
false | true
echo $?
true | false
echo $?
