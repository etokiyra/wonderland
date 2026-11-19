#ifndef WL_AST_ARENA_H
#define WL_AST_ARENA_H

#include <stddef.h>

/*
 * Bump allocator for one parse unit.
 *
 * The AST and every decoded string in it live in an arena; nothing is freed
 * individually. wl_arena_reset() releases the whole parse unit. Blocks are
 * chained and sized geometrically, so a long command does not cost one
 * allocation per node.
 *
 * Allocation returns NULL on failure; callers propagate it as a diagnostic.
 * The arena never aborts.
 */

struct wl_arena_block;
struct wl_arena {
    struct wl_arena_block *head;
};

void wl_arena_init(struct wl_arena *arena);
void wl_arena_reset(struct wl_arena *arena);

/* Zeroed memory, aligned for any type. */
void *wl_arena_alloc(struct wl_arena *arena, size_t size);

/* Zeroed array of count elements; rejects count*size overflow. */
void *wl_arena_calloc(struct wl_arena *arena, size_t count, size_t size);

/* Copies len bytes and appends a NUL. */
const char *wl_arena_str(struct wl_arena *arena, const char *data, size_t len);

#endif /* WL_AST_ARENA_H */
