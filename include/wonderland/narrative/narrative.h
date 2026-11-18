#ifndef WL_NARRATIVE_NARRATIVE_H
#define WL_NARRATIVE_NARRATIVE_H

/*
 * Public boundary of libwl_narrative.a.
 *
 * This header may depend on the C standard library only. It must never
 * mention struct wl_shell, execution, jobs, or any mutable shell state
 * (docs/ARCHITECTURE.md §3.1). Narrative code returns text; the shell decides
 * whether and where to write it (docs/DECISIONS.md D-0015).
 */

/* Identifies the library in diagnostics and link probes. Never user-facing. */
const char *wl_narrative_layer_name(void);

#endif /* WL_NARRATIVE_NARRATIVE_H */
