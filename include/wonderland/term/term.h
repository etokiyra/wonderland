#ifndef WL_TERM_TERM_H
#define WL_TERM_TERM_H

#include <stdbool.h>
#include <termios.h>

/*
 * Terminal snapshot and restoration.
 *
 * M1 never changes terminal modes (input is canonical, the line editor is M5),
 * so restoration is currently a guard rather than a rescue. The snapshot is
 * still real: it is the mechanism M5 will rely on, and it is registered with
 * atexit so a normal `exit` path restores even if main forgets.
 *
 * Later milestones must add a signal-safe restore path before entering raw
 * mode; today there is nothing to restore, and that is stated in
 * docs/ROADMAP.md rather than implied.
 */

struct wl_term {
    int fd;
    bool saved;
    struct termios saved_mode;
};

/* Saves the current settings. Returns false when fd is not a tty. */
bool wl_term_save(struct wl_term *term, int fd);

/* Restores saved settings. Safe to call twice, and with NULL. */
void wl_term_restore(struct wl_term *term);

bool wl_term_is_tty(int fd);

#endif /* WL_TERM_TERM_H */
