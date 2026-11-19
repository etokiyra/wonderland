#ifndef WL_CONTENT_PACK_H
#define WL_CONTENT_PACK_H

#include <stddef.h>

#include "narrative/persona.h"

/*
 * Content pack loader (docs/CONTENT_FORMAT.md).
 *
 * Paths may be files or directories; directories are scanned for *.pack in
 * sorted order. A pack is atomic: any fatal validation error discards that
 * pack with one diagnostic and leaves previously loaded packs untouched.
 * Missing paths are silent; unreadable ones are reported.
 *
 * M6 limitation: one persona per pack. Lines must belong to that persona and
 * are named "<persona>.<anything>". Packs that try to add lines to another
 * pack's persona are rejected (D-0101).
 */
struct wl_packset *wl_packset_load(const char *const *paths, size_t count);
void wl_packset_free(struct wl_packset *packs);

/* Case-sensitive id lookup. */
const struct wl_persona *wl_packset_find(const struct wl_packset *packs,
                                         const char *id);

#endif /* WL_CONTENT_PACK_H */
