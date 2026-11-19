#ifndef WL_JOBS_SIGNAL_H
#define WL_JOBS_SIGNAL_H

#include <stdbool.h>

/*
 * Signal handling, M1 edition.
 *
 * The interactive shell installs a SIGINT handler that only sets a flag; the
 * read loop observes it after read(2) returns EINTR. This is async-signal-safe
 * by construction and needs no self-pipe, because there is no poll() loop yet.
 * M4 introduces the self-pipe for SIGCHLD and job notifications; the SIGINT
 * contract stays "set a flag, do nothing else" (docs/DECISIONS.md D-0045).
 *
 * Interactive mode also ignores SIGQUIT, SIGTSTP, SIGTTIN and SIGTTOU, as a
 * job-controlling shell must. Children reset all of them to SIG_DFL before
 * exec, otherwise an ignored SIGINT would be inherited.
 */

/* Installs the interactive dispositions. Only call when interactive. */
void wl_signal_init_interactive(void);

/* Installs a flag-only handler for a trapped signal (INT or TERM). */
void wl_signal_install_trap(int signo);

/*
 * Restores dispositions in a forked child. With job_control, stopping and
 * terminal-access signals return to default so Ctrl-Z and SIGTTIN work; without
 * it they stay ignored (there is no job table to resume a stopped child).
 */
void wl_signal_reset_for_child(bool job_control);

/* Returns true once if SIGINT arrived since the last call; clears the flag. */
bool wl_signal_take_interrupt(void);

/* True while a SIGINT is pending, without clearing it. */
bool wl_signal_interrupt_pending(void);

/* Returns true once if SIGTERM arrived since the last call; clears the flag. */
bool wl_signal_take_term(void);

/* True while a SIGTERM is pending, without clearing it. */
bool wl_signal_term_pending(void);

#endif /* WL_JOBS_SIGNAL_H */
