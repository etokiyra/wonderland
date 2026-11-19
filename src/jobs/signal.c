#include "jobs/signal.h"

#include <signal.h>
#include <string.h>

/*
 * Signal dispositions.
 *
 * The handlers do exactly one thing: set a sig_atomic_t flag. There is no
 * self-pipe yet because there is no poll() loop to wake; the read loop and the
 * executor observe the flags between commands. No SA_RESTART: restarting the
 * read would swallow the interrupt.
 *
 * SIGINT is installed for the interactive shell and whenever a user trap for
 * INT exists. SIGTERM is installed only when a trap for TERM exists; otherwise
 * its default (terminate) is the correct behaviour.
 */

static volatile sig_atomic_t wl_interrupted;
static volatile sig_atomic_t wl_terminated;

static void wl_on_sigint(int signo) {
    (void)signo;
    wl_interrupted = 1;
}

static void wl_on_sigterm(int signo) {
    (void)signo;
    wl_terminated = 1;
}

static void wl_install_handler(int signo, void (*handler)(int)) {
    struct sigaction action;

    memset(&action, 0, sizeof action);
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    (void)sigaction(signo, &action, NULL);
}

void wl_signal_init_interactive(void) {
    wl_install_handler(SIGINT, wl_on_sigint);

    /*
     * An interactive shell must not die from these, and must not be stopped by
     * terminal ownership changes (it is in the background process group while
     * a foreground job runs).
     */
    (void)signal(SIGQUIT, SIG_IGN);
    (void)signal(SIGTSTP, SIG_IGN);
    (void)signal(SIGTTIN, SIG_IGN);
    (void)signal(SIGTTOU, SIG_IGN);
}

void wl_signal_install_trap(int signo) {
    if (signo == SIGINT) {
        wl_install_handler(SIGINT, wl_on_sigint);
    } else if (signo == SIGTERM) {
        wl_install_handler(SIGTERM, wl_on_sigterm);
    }
}

void wl_signal_reset_for_child(void) {
    /*
     * SIGINT and SIGQUIT must be default: an inherited ignore would make the
     * child immune to Ctrl-C.
     *
     * SIGTSTP/SIGTTIN/SIGTTOU stay ignored until M4. Job control does not
     * exist yet, so a stopped child could never be resumed: waitpid() without
     * WUNTRACED would block forever and the terminal would look hung. Making
     * Ctrl-Z a no-op is the honest behaviour (docs/DECISIONS.md D-0051).
     */
    (void)signal(SIGINT, SIG_DFL);
    (void)signal(SIGQUIT, SIG_DFL);
    (void)signal(SIGTERM, SIG_DFL);
    (void)signal(SIGTSTP, SIG_IGN);
    (void)signal(SIGTTIN, SIG_IGN);
    (void)signal(SIGTTOU, SIG_IGN);
}

bool wl_signal_take_interrupt(void) {
    if (wl_interrupted != 0) {
        wl_interrupted = 0;
        return true;
    }
    return false;
}

bool wl_signal_interrupt_pending(void) {
    return wl_interrupted != 0;
}

bool wl_signal_term_pending(void) {
    return wl_terminated != 0;
}

bool wl_signal_take_term(void) {
    if (wl_terminated != 0) {
        wl_terminated = 0;
        return true;
    }
    return false;
}
