#ifndef WL_JOBS_JOB_H
#define WL_JOBS_JOB_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct wl_shell;
struct wl_node;

/*
 * Job table.
 *
 * A job is a process group: one process for `cmd &` or a stopped simple
 * command, several for a pipeline. The module owns only bookkeeping and
 * waiting; it never executes anything and never touches the AST. The shell
 * owns one table directly (shell.h embeds it), so there is no allocation for
 * an empty table.
 *
 * Ids are 1-based and never reused within a session, matching what `jobs`,
 * `fg %1` and the notifications show.
 */

enum wl_job_state {
    WL_JOB_RUNNING,
    WL_JOB_STOPPED,
    WL_JOB_DONE
};

struct wl_proc {
    pid_t pid;
    int status; /* raw wait(2) status */
    bool done;
    bool stopped;
};

struct wl_job {
    int id;
    pid_t pgid;
    enum wl_job_state state;
    struct wl_proc *procs;
    size_t proc_count;
    char *command; /* printable, owned */
    bool background;
    bool notified; /* stopped/done message already printed */
    struct wl_job *next;
};

struct wl_jobtab {
    struct wl_job *head;
    int next_id;
};

void wl_jobtab_init(struct wl_jobtab *table);
void wl_jobtab_free(struct wl_jobtab *table);

/* Creates and links a job. Returns NULL on allocation failure. */
struct wl_job *wl_job_create(struct wl_jobtab *table, pid_t pgid,
                             const char *command, bool background);

/* Appends a process to the job. Returns false on allocation failure. */
bool wl_job_add_proc(struct wl_job *job, pid_t pid);

struct wl_job *wl_job_find_id(struct wl_jobtab *table, int id);
struct wl_job *wl_job_find_pid(struct wl_jobtab *table, pid_t pid);
struct wl_job *wl_job_current(struct wl_jobtab *table);
struct wl_job *wl_job_previous(struct wl_jobtab *table);

void wl_job_remove(struct wl_jobtab *table, struct wl_job *job);

int wl_job_status(const struct wl_job *job);
bool wl_job_all_done(const struct wl_job *job);
bool wl_job_any_stopped(const struct wl_job *job);

/* Hands the terminal to the job's process group (no-op when not a tty). */
void wl_job_give_terminal(struct wl_shell *shell, pid_t pgid);

/*
 * Prepares interactive job control: the shell gets its own process group and
 * takes the terminal. No-op without a tty.
 */
void wl_job_init(struct wl_shell *shell);

/*
 * Blocks until every process in the job is done or stopped. Updates names on
 * `ps` output? No: updates the job state and returns the last status.
 */
int wl_job_wait(struct wl_shell *shell, struct wl_job *job, bool *stopped);

/* Resumes the job in the foreground; waits; returns its status. */
int wl_job_foreground(struct wl_shell *shell, struct wl_job *job);

/* Resumes the job in the background. */
void wl_job_background(struct wl_shell *shell, struct wl_job *job);

/*
 * Reaps children without blocking, updates the table, and prints
 * Done/Stopped notifications in an interactive shell. Call before the prompt
 * and from `jobs`/`fg`/`bg`/`wait`.
 */
void wl_job_notify(struct wl_shell *shell);

/*
 * Builds a one-line printable command for a node from the shell's current
 * source span. Returns a malloc'd string (never NULL unless OOM).
 */
char *wl_job_command_text(struct wl_shell *shell, const struct wl_node *node);

#endif /* WL_JOBS_JOB_H */
