#ifndef WL_CORE_LIMITS_H
#define WL_CORE_LIMITS_H

/*
 * Fixed limits for the M1 shell.
 *
 * These are deliberately modest and explicit. A line that exceeds them is
 * refused with a diagnostic, never truncated silently. The lexer/parser (M2)
 * replaces WL_LINE_MAX-based splitting and gets its own, documented limits
 * (docs/GRAMMAR.md §7).
 */

#define WL_LINE_MAX 4096
#define WL_ARGV_MAX 256
#define WL_PATH_MAX 4096

#endif /* WL_CORE_LIMITS_H */
