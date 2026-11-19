# Exit statuses and list operators.
true
echo $?
false
echo $?
true && echo and
false && echo no
false || echo or
true || echo no
! true
echo $?
false | true
echo $?
