#include "narrative.h"

/*
 * Placeholder translation unit for libwl_narrative.a.
 *
 * It exists so that the archive is real, buildable, and link-probeable from
 * M0 (docs/DECISIONS.md D-0032). It contains no personas, no events, and no
 * content: the vertical slice in M3.5 replaces this with the actual
 * implementation. It must never grow a dependency on shell state; only the
 * C standard library is visible here, by construction of the include path
 * (docs/ARCHITECTURE.md §3.1).
 */

const char *wl_narrative_layer_name(void) {
    return "narrative";
}
