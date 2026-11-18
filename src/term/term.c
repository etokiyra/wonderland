#include "term/term.h"

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * Terminal snapshot. M1 does not change any modes, so this is currently a
 * guard for M5 rather than a rescue; it still saves and restores for real, and
 * it is wired to atexit so a normal exit cannot leave settings behind.
 */

static struct wl_term *wl_term_current;

static void wl_term_atexit(void) {
    wl_term_restore(wl_term_current);
}

static void wl_term_install_atexit(void) {
    static bool installed;

    if (!installed) {
        (void)atexit(wl_term_atexit);
        installed = true;
    }
}

bool wl_term_is_tty(int fd) {
    return isatty(fd) == 1;
}

bool wl_term_save(struct wl_term *term, int fd) {
    if (term == NULL) {
        return false;
    }
    term->fd = fd;
    term->saved = false;

    if (!wl_term_is_tty(fd)) {
        return false;
    }
    if (tcgetattr(fd, &term->saved_mode) != 0) {
        return false;
    }

    term->saved = true;
    wl_term_current = term;
    wl_term_install_atexit();
    return true;
}

void wl_term_restore(struct wl_term *term) {
    if (term == NULL || !term->saved) {
        return;
    }
    /* TCSADRAIN: flush output we have already written, do not eat typed input. */
    (void)tcsetattr(term->fd, TCSADRAIN, &term->saved_mode);
    term->saved = false;
    if (wl_term_current == term) {
        wl_term_current = NULL;
    }
}
