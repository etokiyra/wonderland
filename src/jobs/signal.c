#include "jobs/signal.h"

#include <signal.h>
#include <string.h>

/*
 * Signal dispositions for M1.
 *
 * The SIGINT handler does exactly one thing: set a sig_atomic_t flag. There is
 * no self-pipe yet because there is no poll() loop to wake; the read loop
 * observes the flag after read(2) returns EINTR. No SA_RESTART: restarting the
 * read would swallow the interrupt.
 */

static volatile sig_atomic_t wl_interrupted;

static void wl_on_sigint(int signo) {
    (void)signo;
    wl_interrupted = 1;
}

void wl_signal_init_interactive(void) {
    struct sigaction action;

    memset(&action, 0, sizeof action);
    action.sa_handler = wl_on_sigint;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    (void)sigaction(SIGINT, &action, NULL);

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

void wl_signal_reset_for_child(void) {
    /*
     * SIGINT and SIGQUIT must be default: an inherited ignore would make the
     * child immune to Ctrl-C.
     *
     * SIGTSTP/SIGTTIN/SIGTTOU stay ignored until M4. Job control does not
     * exist yet, so a stopped child could never be resumed: waitpid() without
     * WUNTRACED would block forever and the terminal would look hung. Making
     * Ctrl-Z a no-op is the honest M1 behaviour (docs/DECISIONS.md D-0051).
     */
    (void)signal(SIGINT, SIG_DFL);
    (void)signal(SIGQUIT, SIG_DFL);
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
