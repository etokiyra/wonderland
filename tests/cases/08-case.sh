# case with alternation and glob patterns.
for x in foo bar baz qux; do
    case $x in
        f*) printf 'F:%s\n' "$x" ;;
        b??) printf 'B:%s\n' "$x" ;;
        *) printf 'O:%s\n' "$x" ;;
    esac
done

case zzz in
    a | b) echo no ;;
esac
echo done
