#include "ast/arena.h"
#include "ast/ast.h"
#include "core/buf.h"
#include "core/diag.h"
#include "parse/parse.h"
#include "test.h"

#include <stdint.h>
#include <string.h>

/*
 * Deterministic fuzz pass over the parser.
 *
 * Not coverage-guided and not a substitute for one: it is a cheap guard
 * against crashes, runaway loops, and leaks on malformed input, and it runs in
 * normal `make test` (where ASan makes it much more useful).
 */

static uint64_t wl_fuzz_state = 0x9e3779b97f4a7c15ULL;

static uint64_t wl_fuzz_next(void) {
    wl_fuzz_state ^= wl_fuzz_state << 13;
    wl_fuzz_state ^= wl_fuzz_state >> 7;
    wl_fuzz_state ^= wl_fuzz_state << 17;
    return wl_fuzz_state;
}

WL_TEST(parser_survives_random_bytes) {
    static const char alphabet[] =
        "abcXYZ  \t\n'\"\\$(){}|&;<>#*=~!@%?-+0123456789";
    struct wl_arena arena;
    struct wl_buf dump;
    enum { ITERATIONS = 10000 };

    wl_arena_init(&arena);
    wl_buf_init(&dump);

    for (unsigned iteration = 0; iteration < ITERATIONS; iteration++) {
        char input[80];
        size_t length = (size_t)(wl_fuzz_next() % 72);
        struct wl_diag diag;
        struct wl_node *node;

        for (size_t index = 0; index < length; index++) {
            input[index] =
                alphabet[wl_fuzz_next() % (sizeof alphabet - 1)];
        }
        input[length] = '\0';

        wl_diag_clear(&diag);
        node = wl_parse_source(input, length, "fuzz", &arena, &diag);
        if (node != NULL) {
            wl_buf_clear(&dump);
            wl_ast_dump(node, &dump);
        }
        wl_arena_reset(&arena);
    }

    WL_ASSERT(1); /* reaching here at all is the assertion */
    wl_buf_free(&dump);
}

void wl_suite_fuzz(void) {
    wl_test_register("fuzz: random input does not crash the parser",
                     parser_survives_random_bytes);
}
