#include "ast/arena.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WL_ARENA_BLOCK_MIN 4096

struct wl_arena_block {
    struct wl_arena_block *next;
    size_t used;
    size_t capacity;
    /* Forces data[] to start at an alignment suitable for any type. */
    max_align_t alignment_guard;
    unsigned char data[];
};

static size_t wl_arena_align(size_t value) {
    size_t alignment = _Alignof(max_align_t);

    return (value + alignment - 1) & ~(alignment - 1);
}

void wl_arena_init(struct wl_arena *arena) {
    arena->head = NULL;
}

void wl_arena_reset(struct wl_arena *arena) {
    struct wl_arena_block *block = arena->head;

    while (block != NULL) {
        struct wl_arena_block *next = block->next;

        free(block);
        block = next;
    }
    arena->head = NULL;
}

static struct wl_arena_block *wl_arena_add(struct wl_arena *arena,
                                           size_t minimum) {
    size_t capacity = WL_ARENA_BLOCK_MIN;
    struct wl_arena_block *block;

    if (capacity < minimum) {
        capacity = minimum;
    }
    if (capacity > SIZE_MAX - sizeof *block) {
        return NULL;
    }

    block = malloc(sizeof *block + capacity);
    if (block == NULL) {
        return NULL;
    }
    block->next = arena->head;
    block->used = 0;
    block->capacity = capacity;
    arena->head = block;
    return block;
}

void *wl_arena_alloc(struct wl_arena *arena, size_t size) {
    struct wl_arena_block *block;
    size_t needed;
    size_t offset;

    if (size == 0) {
        size = 1;
    }
    needed = wl_arena_align(size);

    block = arena->head;
    if (block == NULL || block->used + needed > block->capacity) {
        block = wl_arena_add(arena, needed);
        if (block == NULL) {
            return NULL;
        }
    }

    offset = block->used;
    block->used += needed;
    memset(block->data + offset, 0, needed);
    return block->data + offset;
}

void *wl_arena_calloc(struct wl_arena *arena, size_t count, size_t size) {
    if (size != 0 && count > SIZE_MAX / size) {
        return NULL;
    }
    return wl_arena_alloc(arena, count * size);
}

const char *wl_arena_str(struct wl_arena *arena, const char *data, size_t len) {
    char *copy;

    if (len > SIZE_MAX - 1) {
        return NULL;
    }
    copy = wl_arena_alloc(arena, len + 1);
    if (copy == NULL) {
        return NULL;
    }
    if (len > 0) {
        memcpy(copy, data, len);
    }
    copy[len] = '\0';
    return copy;
}
