#include "ast/arena.h"
#include "ast/ast.h"
#include "core/buf.h"
#include "core/diag.h"
#include "parse/parse.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

/*
 * Parser tests through the public interface and the stable dump.
 *
 * Exact dumps are used where the shape matters and is small; substring
 * checks are used for large compound nodes, where an exact dump would be
 * unreadable and would break on harmless formatting changes.
 */

struct fixture {
    struct wl_arena arena;
    struct wl_buf dump;
    struct wl_diag diag;
};

static void fixture_open(struct fixture *fixture) {
    wl_arena_init(&fixture->arena);
    wl_buf_init(&fixture->dump);
    wl_diag_clear(&fixture->diag);
}

static void fixture_close(struct fixture *fixture) {
    wl_buf_free(&fixture->dump);
    wl_arena_reset(&fixture->arena);
}

static const char *fixture_dump(struct fixture *fixture, const char *source) {
    struct wl_node *node;

    wl_diag_clear(&fixture->diag);
    wl_buf_clear(&fixture->dump);
    node = wl_parse_source(source, strlen(source), "test", &fixture->arena,
                           &fixture->diag);
    if (node == NULL) {
        return NULL;
    }
    wl_ast_dump(node, &fixture->dump);
    return (fixture->dump.data != NULL) ? fixture->dump.data : "";
}

static int fixture_fails(struct fixture *fixture, const char *source,
                         bool *incomplete) {
    struct wl_node *node;

    wl_diag_clear(&fixture->diag);
    node = wl_parse_source(source, strlen(source), "test", &fixture->arena,
                           &fixture->diag);
    if (node != NULL) {
        return 0;
    }
    if (incomplete != NULL) {
        *incomplete = fixture->diag.incomplete;
    }
    return fixture->diag.set;
}

WL_TEST(simple_command_has_a_stable_dump) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "echo hi");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_STR_EQ(dump,
                         "(list\n"
                         "  (andor\n"
                         "    (pipeline\n"
                         "      (simple\n"
                         "        (word (lit u \"echo\"))\n"
                         "        (word (lit u \"hi\"))))))");
    }
    fixture_close(&fixture);
}

WL_TEST(quoting_survives_into_word_parts) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "echo \"a b\" 'c d' e\\ f");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(lit q \"a b\")");
        WL_ASSERT_CONTAINS(dump, "(lit q \"c d\")");
        /* An escaped space is its own quoted run between unquoted runs. */
        WL_ASSERT_CONTAINS(dump,
                           "(word (lit u \"e\") (lit q \" \") (lit u \"f\"))");
    }
    fixture_close(&fixture);
}

WL_TEST(empty_quotes_produce_an_empty_quoted_part) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "echo '' \"\" x\"\"y");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(word (lit q \"\"))");
        /* Adjacent literal runs merge: x""y is one unquoted word "xy". */
        WL_ASSERT_CONTAINS(dump, "(word (lit u \"xy\"))");
    }
    fixture_close(&fixture);
}

WL_TEST(operators_build_the_expected_shapes) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "a | b && c || d");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(pipeline");
        WL_ASSERT_CONTAINS(dump, "(and");
        WL_ASSERT_CONTAINS(dump, "(or");
    }
    fixture_close(&fixture);

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "sleep 1 &");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(async");
    }
    fixture_close(&fixture);
}

WL_TEST(assignments_are_split_from_words) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "A=1 B=\"x y\" cmd arg");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(assign A (word (lit u \"1\")))");
        WL_ASSERT_CONTAINS(dump, "(assign B (word (lit q \"x y\")))");
        WL_ASSERT_CONTAINS(dump, "(word (lit u \"cmd\"))");
        WL_ASSERT_CONTAINS(dump, "(word (lit u \"arg\"))");
    }
    fixture_close(&fixture);
}

WL_TEST(parameter_expansions_keep_their_operators) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture,
                        "echo $x ${y:-z} ${#p} $1 $@ ${q#pre} ${r%%suf}");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(param u name=x)");
        WL_ASSERT_CONTAINS(dump, "(param u name=y op=:- (word (lit u \"z\")))");
        WL_ASSERT_CONTAINS(dump, "(param u name=p op=length)");
        WL_ASSERT_CONTAINS(dump, "(param u index=1)");
        WL_ASSERT_CONTAINS(dump, "(param u special=@)");
        WL_ASSERT_CONTAINS(dump, "(param u name=q op=# (word (lit u \"pre\")))");
        WL_ASSERT_CONTAINS(dump, "(param u name=r op=%% (word (lit u \"suf\")))");
    }
    fixture_close(&fixture);
}

WL_TEST(command_substitution_is_parsed_as_a_program) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "echo $(echo hi)");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(cmdsub u");
        WL_ASSERT_CONTAINS(dump, "(word (lit u \"hi\"))");
    }
    fixture_close(&fixture);
}

WL_TEST(arithmetic_is_stored_as_source) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "echo $((1 + 2))");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(arith u \"1 + 2\")");
    }
    fixture_close(&fixture);
}

WL_TEST(tilde_is_only_special_at_word_start) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "echo ~/dir a~b");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(word (tilde u \"~\") (lit u \"/dir\"))");
        WL_ASSERT_CONTAINS(dump, "(word (lit u \"a~b\"))");
    }
    fixture_close(&fixture);
}

WL_TEST(redirections_keep_their_fds) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "cmd 2>&1 >out <in");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(redir 2>& (word (lit u \"1\")))");
        WL_ASSERT_CONTAINS(dump, "(redir 1> (word (lit u \"out\")))");
        WL_ASSERT_CONTAINS(dump, "(redir 0< (word (lit u \"in\")))");
    }
    fixture_close(&fixture);
}

WL_TEST(heredoc_bodies_are_collected) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "cat <<EOF\nhi $x\nEOF\n");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "body=\"hi $x\\n\" expand");
    }
    fixture_close(&fixture);

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "cat <<'EOF'\nhi $x\nEOF\n");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "body=\"hi $x\\n\" literal");
    }
    fixture_close(&fixture);

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "cat <<-EOF\n\thi\n\tEOF\n");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "body=\"hi\\n\" expand strip-tabs");
    }
    fixture_close(&fixture);
}

WL_TEST(compound_commands_parse) {
    static const struct {
        const char *source;
        const char *needle;
    } cases[] = {
        {"if a; then b; elif c; then d; else e; fi", "(if"},
        {"for i in a b; do echo $i; done", "(for name=i"},
        {"for i; do echo $i; done", "(for name=i"},
        {"while a; do b; done", "(while"},
        {"until a; do b; done", "(until"},
        {"case $x in a|b) echo ab;; *) echo other;; esac", "(case"},
        {"{ echo x; }", "(group"},
        {"(echo x)", "(subshell"},
        {"f() { echo f; }", "(func name=f"},
    };

    for (size_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        struct fixture fixture;
        const char *dump;

        fixture_open(&fixture);
        dump = fixture_dump(&fixture, cases[index].source);
        WL_ASSERT(dump != NULL);
        if (dump != NULL && strstr(dump, cases[index].needle) == NULL) {
            printf("case failed: %s\n%s\n", cases[index].source, dump);
        }
        WL_ASSERT(dump != NULL && strstr(dump, cases[index].needle) != NULL);
        fixture_close(&fixture);
    }
}

WL_TEST(reserved_words_are_ordinary_arguments) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "echo if then fi done");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(lit u \"if\")");
        WL_ASSERT_CONTAINS(dump, "(lit u \"then\")");
        WL_ASSERT_CONTAINS(dump, "(lit u \"fi\")");
        WL_ASSERT_CONTAINS(dump, "(lit u \"done\")");
        WL_ASSERT(!strstr(dump, "(if"));
    }
    fixture_close(&fixture);
}

WL_TEST(comments_and_blank_lines_are_ignored) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "\n# comment\n\necho hi # trailing\n");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(lit u \"hi\")");
        WL_ASSERT(strstr(dump, "comment") == NULL);
    }
    fixture_close(&fixture);
}

WL_TEST(line_continuation_is_removed) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "echo ab\\\ncd");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(lit u \"abcd\")");
    }
    fixture_close(&fixture);
}

WL_TEST(incomplete_input_is_marked_incomplete) {
    static const char *const cases[] = {
        "echo \"abc",
        "echo 'abc",
        "echo $(echo",
        "echo $((1 +",
        "if true; then",
        "while true; do",
        "for i in a b; do",
        "case x in a) echo a;;",
        "{ echo x;",
        "f() {",
        "cat <<EOF",
        "cat <<EOF\nbody\n",
    };

    for (size_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        struct fixture fixture;
        bool incomplete = false;

        fixture_open(&fixture);
        WL_ASSERT(fixture_fails(&fixture, cases[index], &incomplete));
        if (!incomplete) {
            printf("expected incomplete: %s\n", cases[index]);
        }
        WL_ASSERT(incomplete);
        fixture_close(&fixture);
    }
}

WL_TEST(errors_are_not_reported_as_incomplete) {
    struct fixture fixture;
    bool incomplete = true;

    fixture_open(&fixture);
    WL_ASSERT(fixture_fails(&fixture, "if true; fi", &incomplete));
    WL_ASSERT(!incomplete);
    WL_ASSERT_CONTAINS(fixture.diag.message, "then");
    fixture_close(&fixture);

    fixture_open(&fixture);
    WL_ASSERT(fixture_fails(&fixture, "| foo", &incomplete));
    WL_ASSERT(!incomplete);
    fixture_close(&fixture);

    fixture_open(&fixture);
    WL_ASSERT(fixture_fails(&fixture, "echo ${}", &incomplete));
    WL_ASSERT_CONTAINS(fixture.diag.message, "bad substitution");
    fixture_close(&fixture);
}

WL_TEST(nesting_is_bounded) {
    struct fixture fixture;
    char source[1200];
    size_t at = 0;
    bool incomplete = false;

    for (int index = 0; index < 250; index++) {
        source[at++] = '(';
    }
    source[at++] = 'x';
    for (int index = 0; index < 250; index++) {
        source[at++] = ')';
    }
    source[at] = '\0';

    fixture_open(&fixture);
    WL_ASSERT(fixture_fails(&fixture, source, &incomplete));
    WL_ASSERT_CONTAINS(fixture.diag.message, "too deep");
    fixture_close(&fixture);
}

WL_TEST(quoting_matrix_from_the_grammar) {
    struct fixture fixture;
    const char *dump;

    /* Unquoted `|` is an operator: two commands. */
    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "a|b");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(pipeline");
        WL_ASSERT_CONTAINS(dump, "(simple");
    }
    fixture_close(&fixture);

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "a\\|b");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump,
                           "(word (lit u \"a\") (lit q \"|\") (lit u \"b\"))");
    }
    fixture_close(&fixture);

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "a'b|c'd");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(lit q \"b|c\")");
    }
    fixture_close(&fixture);

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "a\"b|c\"d");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(lit q \"b|c\")");
    }
    fixture_close(&fixture);

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "echo a\"\"b");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(word (lit u \"ab\"))");
    }
    fixture_close(&fixture);
}

WL_TEST(parameter_parts_remember_quoting) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "echo $x \"$x\" \"$@\" ''");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(param u name=x)");
        WL_ASSERT_CONTAINS(dump, "(param q name=x)");
        WL_ASSERT_CONTAINS(dump, "(param q special=@)");
        WL_ASSERT_CONTAINS(dump, "(lit q \"\")");
    }
    fixture_close(&fixture);
}

WL_TEST(assignment_values_keep_expansion_structure) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "a=1 b=$a cmd");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(assign b (word (param u name=a)))");
    }
    fixture_close(&fixture);
}

WL_TEST(heredoc_inside_a_compound_command) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture,
                        "while read x; do cat <<EOF\nhi\nEOF\ndone\n");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "body=\"hi\\n\"");
        WL_ASSERT_CONTAINS(dump, "(while");
    }
    fixture_close(&fixture);
}

WL_TEST(reserved_word_edge_cases) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "echo \"if\" \\if; if=1 cmd");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(word (lit q \"if\"))");
        WL_ASSERT_CONTAINS(dump, "(lit q \"if\")");
        WL_ASSERT_CONTAINS(dump, "(assign if (word (lit u \"1\")))");
        WL_ASSERT(!strstr(dump, "(if"));
    }
    fixture_close(&fixture);
}

WL_TEST(redirection_only_command_parses) {
    struct fixture fixture;
    const char *dump;

    fixture_open(&fixture);
    dump = fixture_dump(&fixture, "> out 2>&1");
    WL_ASSERT(dump != NULL);
    if (dump != NULL) {
        WL_ASSERT_CONTAINS(dump, "(simple");
        WL_ASSERT_CONTAINS(dump, "(redir 1> (word (lit u \"out\")))");
        WL_ASSERT_CONTAINS(dump, "(redir 2>& (word (lit u \"1\")))");
    }
    fixture_close(&fixture);
}

void wl_suite_ast(void) {
    wl_test_register("ast: simple command dump", simple_command_has_a_stable_dump);
    wl_test_register("ast: quoting into parts", quoting_survives_into_word_parts);
    wl_test_register("ast: empty quotes", empty_quotes_produce_an_empty_quoted_part);
    wl_test_register("ast: quoting matrix", quoting_matrix_from_the_grammar);
    wl_test_register("ast: parameter quoting", parameter_parts_remember_quoting);
    wl_test_register("ast: assignment values", assignment_values_keep_expansion_structure);
    wl_test_register("ast: heredoc in compound", heredoc_inside_a_compound_command);
    wl_test_register("ast: reserved word edges", reserved_word_edge_cases);
    wl_test_register("ast: operators", operators_build_the_expected_shapes);
    wl_test_register("ast: assignments", assignments_are_split_from_words);
    wl_test_register("ast: parameter expansions", parameter_expansions_keep_their_operators);
    wl_test_register("ast: command substitution", command_substitution_is_parsed_as_a_program);
    wl_test_register("ast: arithmetic source", arithmetic_is_stored_as_source);
    wl_test_register("ast: tilde", tilde_is_only_special_at_word_start);
    wl_test_register("ast: redirections", redirections_keep_their_fds);
    wl_test_register("ast: redirection-only", redirection_only_command_parses);
    wl_test_register("ast: here-documents", heredoc_bodies_are_collected);
    wl_test_register("ast: compound commands", compound_commands_parse);
    wl_test_register("ast: reserved words as arguments", reserved_words_are_ordinary_arguments);
    wl_test_register("ast: comments", comments_and_blank_lines_are_ignored);
    wl_test_register("ast: line continuation", line_continuation_is_removed);
    wl_test_register("ast: incomplete input", incomplete_input_is_marked_incomplete);
    wl_test_register("ast: hard errors", errors_are_not_reported_as_incomplete);
    wl_test_register("ast: nesting limit", nesting_is_bounded);
}
