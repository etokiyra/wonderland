#include "jobs/job.h"

#include "ast/ast.h"
#include "core/shell.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Job table and waiting.
 *
 * The shell is single-threaded and reaps synchronously: foreground commands
 * wait with WUNTRACED (so Ctrl-Z produces a stopped job), and between prompts
 * wl_job_notify() drains finished background children with WNOHANG. SIGCHLD
 * still only interrupts; no handler does work.
 */

void wl_jobtab_init(struct wl_jobtab *table) {
    table->head = NULL;
    table->next_id = 1;
}

static void wl_job_free(struct wl_job *job) {
    free(job->procs);
    free(job->command);
    free(job);
}

void wl_jobtab_free(struct wl_jobtab *table) {
    struct wl_job *job = table->head;

    while (job != NULL) {
        struct wl_job *next = job->next;

        wl_job_free(job);
        job = next;
    }
    table->head = NULL;
    free(table->last_finished_command);
    table->last_finished_command = NULL;
    table->started = 0;
    table->finished = 0;
}

struct wl_job *wl_job_create(struct wl_jobtab *table, pid_t pgid,
                             const char *command, bool background) {
    struct wl_job *job = calloc(1, sizeof *job);

    if (job == NULL) {
        return NULL;
    }
    job->id = table->next_id++;
    job->pgid = pgid;
    job->state = WL_JOB_RUNNING;
    job->background = background;
    job->command = strdup((command != NULL) ? command : "");
    if (job->command == NULL) {
        free(job);
        return NULL;
    }
    job->next = table->head;
    table->head = job;
    table->started++;
    return job;
}

bool wl_job_add_proc(struct wl_job *job, pid_t pid) {
    struct wl_proc *grown =
        realloc(job->procs, (job->proc_count + 1) * sizeof *grown);

    if (grown == NULL) {
        return false;
    }
    job->procs = grown;
    job->procs[job->proc_count].pid = pid;
    job->procs[job->proc_count].status = 0;
    job->procs[job->proc_count].done = false;
    job->procs[job->proc_count].stopped = false;
    job->proc_count++;
    return true;
}

struct wl_job *wl_job_find_id(struct wl_jobtab *table, int id) {
    for (struct wl_job *job = table->head; job != NULL; job = job->next) {
        if (job->id == id) {
            return job;
        }
    }
    return NULL;
}

struct wl_job *wl_job_find_pid(struct wl_jobtab *table, pid_t pid) {
    for (struct wl_job *job = table->head; job != NULL; job = job->next) {
        for (size_t index = 0; index < job->proc_count; index++) {
            if (job->procs[index].pid == pid) {
                return job;
            }
        }
    }
    return NULL;
}

struct wl_job *wl_job_current(struct wl_jobtab *table) {
    return table->head;
}

struct wl_job *wl_job_previous(struct wl_jobtab *table) {
    return (table->head != NULL) ? table->head->next : NULL;
}

void wl_job_remove(struct wl_jobtab *table, struct wl_job *job) {
    struct wl_job **link = &table->head;

    while (*link != NULL) {
        if (*link == job) {
            if (job->state == WL_JOB_DONE) {
                char *copy = strdup(job->command);

                if (copy != NULL) {
                    free(table->last_finished_command);
                    table->last_finished_command = copy;
                }
                table->finished++;
            }
            *link = job->next;
            wl_job_free(job);
            return;
        }
        link = &(*link)->next;
    }
}

bool wl_job_all_done(const struct wl_job *job) {
    for (size_t index = 0; index < job->proc_count; index++) {
        if (!job->procs[index].done) {
            return false;
        }
    }
    return job->proc_count > 0;
}

bool wl_job_any_stopped(const struct wl_job *job) {
    for (size_t index = 0; index < job->proc_count; index++) {
        if (job->procs[index].stopped && !job->procs[index].done) {
            return true;
        }
    }
    return false;
}

int wl_job_status(const struct wl_job *job) {
    int status = 0;

    for (size_t index = 0; index < job->proc_count; index++) {
        const struct wl_proc *proc = &job->procs[index];

        if (WIFEXITED(proc->status)) {
            status = WEXITSTATUS(proc->status);
        } else if (WIFSIGNALED(proc->status)) {
            status = 128 + WTERMSIG(proc->status);
        } else if (WIFSTOPPED(proc->status)) {
            status = 128 + WSTOPSIG(proc->status);
        }
    }
    return status;
}

void wl_job_init(struct wl_shell *shell) {
    if (shell->tty_fd < 0) {
        return;
    }
    /*
     * Put the shell in its own process group and take the terminal. Failure
     * with EPERM/EACCES means we are already a group leader, which is fine.
     */
    (void)setpgid(0, 0);
    shell->shell_pgid = getpgrp();
    wl_job_give_terminal(shell, shell->shell_pgid);
}

void wl_job_give_terminal(struct wl_shell *shell, pid_t pgid) {
    if (shell->tty_fd < 0) {
        return;
    }
    while (tcsetpgrp(shell->tty_fd, pgid) != 0 && errno == EINTR) {
        /* retry: a signal can interrupt the ioctl */
    }
}

static void wl_job_update_state(struct wl_job *job, bool stopped) {
    if (wl_job_all_done(job)) {
        job->state = WL_JOB_DONE;
    } else if (stopped || wl_job_any_stopped(job)) {
        job->state = WL_JOB_STOPPED;
    } else {
        job->state = WL_JOB_RUNNING;
    }
}

int wl_job_wait(struct wl_shell *shell, struct wl_job *job, bool *stopped) {
    bool any_stopped = false;

    (void)shell;
    if (stopped != NULL) {
        *stopped = false;
    }

    for (size_t index = 0; index < job->proc_count; index++) {
        struct wl_proc *proc = &job->procs[index];
        int status = 0;
        pid_t reaped;

        if (proc->done) {
            continue;
        }
        do {
            reaped = waitpid(proc->pid, &status, WUNTRACED);
        } while (reaped < 0 && errno == EINTR);

        if (reaped < 0) {
            /* Already reaped elsewhere (notification pass): treat as done. */
            proc->done = true;
            continue;
        }
        proc->status = status;
        if (WIFSTOPPED(status)) {
            proc->stopped = true;
            any_stopped = true;
        } else {
            proc->done = true;
            proc->stopped = false;
        }
    }

    wl_job_update_state(job, any_stopped);
    if (stopped != NULL) {
        *stopped = any_stopped;
    }
    return wl_job_status(job);
}

int wl_job_foreground(struct wl_shell *shell, struct wl_job *job) {
    bool stopped = false;
    int status;

    job->notified = false;
    wl_job_give_terminal(shell, job->pgid);
    if (kill(-job->pgid, SIGCONT) != 0 && errno != ESRCH) {
        /* continue anyway: the job may already be finishing */
    }
    job->state = WL_JOB_RUNNING;

    status = wl_job_wait(shell, job, &stopped);
    wl_job_give_terminal(shell, shell->shell_pgid);

    if (stopped) {
        job->state = WL_JOB_STOPPED;
        fprintf(stderr, "[%d]+ Stopped %s\n", job->id, job->command);
    } else {
        wl_job_remove(&shell->jobs, job);
    }
    return status;
}

void wl_job_background(struct wl_shell *shell, struct wl_job *job) {
    (void)shell;
    job->notified = false;
    job->state = WL_JOB_RUNNING;
    if (kill(-job->pgid, SIGCONT) != 0 && errno != ESRCH) {
        fprintf(stderr, "wonderland: bg: %s\n", strerror(errno));
        return;
    }
    fprintf(stderr, "[%d]+ %s &\n", job->id, job->command);
}

void wl_job_notify(struct wl_shell *shell) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        struct wl_job *job = wl_job_find_pid(&shell->jobs, pid);

        if (job == NULL) {
            continue;
        }
        for (size_t index = 0; index < job->proc_count; index++) {
            struct wl_proc *proc = &job->procs[index];

            if (proc->pid != pid) {
                continue;
            }
            proc->status = status;
            if (WIFSTOPPED(status)) {
                proc->stopped = true;
            } else if (WIFCONTINUED(status)) {
                proc->stopped = false;
            } else {
                proc->done = true;
                proc->stopped = false;
            }
        }
        wl_job_update_state(job, false);
    }

    {
        struct wl_job *job = shell->jobs.head;

        while (job != NULL) {
            struct wl_job *next = job->next;

            if (job->state == WL_JOB_DONE) {
                if (shell->interactive) {
                    fprintf(stderr, "[%d]+ Done %s\n", job->id, job->command);
                }
                wl_job_remove(&shell->jobs, job);
            } else if (job->state == WL_JOB_STOPPED && !job->notified) {
                if (shell->interactive) {
                    fprintf(stderr, "[%d]+ Stopped %s\n", job->id,
                            job->command);
                }
                job->notified = true;
            }
            job = next;
        }
    }
}

char *wl_job_command_text(struct wl_shell *shell, const struct wl_node *node) {
    size_t length;
    size_t start;
    size_t end;
    char *text;
    size_t out = 0;

    if (shell->source == NULL || node == NULL) {
        return strdup("?");
    }
    start = node->span.start;
    end = node->span.end;
    if (end < start || start > shell->source_length) {
        return strdup("?");
    }
    if (end > shell->source_length) {
        end = shell->source_length;
    }
    length = end - start;
    if (length > 120) {
        length = 120;
    }

    text = malloc(length + 1);
    if (text == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < length; index++) {
        char c = shell->source[start + index];

        if (c == '\n' || c == '\t') {
            c = ' ';
        }
        if (c != '\r') {
            text[out++] = c;
        }
    }
    text[out] = '\0';
    return text;
}
