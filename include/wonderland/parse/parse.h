#ifndef WL_PARSE_PARSE_H
#define WL_PARSE_PARSE_H

#include <stddef.h>

#include "ast/ast.h"
#include "core/diag.h"

/*
 * Parser for the grammar in docs/GRAMMAR.md.
 *
 * One call parses one complete source unit (a line, a -c string, or a whole
 * stdin buffer). On failure it returns NULL and fills diag; if diag.incomplete
 * is set, the input ended somewhere that more input could still complete
 * (open quote, open `if`, pending here-document). The interactive loop uses
 * that to continue reading; scripts treat it as a syntax error.
 *
 * Recursion is bounded by WL_PARSE_MAX_DEPTH (docs/GRAMMAR.md §7).
 */

#define WL_PARSE_MAX_DEPTH 200

struct wl_node *wl_parse_source(const char *source, size_t length,
                                const char *name, struct wl_arena *arena,
                                struct wl_diag *diag);

#endif /* WL_PARSE_PARSE_H */
