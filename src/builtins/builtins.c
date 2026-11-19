#include "builtins/builtins.h"

#include "core/limits.h"
#include "core/shell.h"
#include "core/var.h"
#include "exec/exec.h"
#include "jobs/job.h"
#include "jobs/signal.h"
#include "line/read.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Builtins. They own their state changes (cwd, variables, positional
 * parameters, control-flow flags, traps, loop termination) and return a
 * status like any command. No narrative, no aliases; those are M6/M7 and
 * belong in the same functions when they arrive.
 */

static bool wl_is_name(const char *text, size_t length) {
    if (length == 0) {
        return false;
    }
    if (!((text[0] >= 'a' && text[0] <= 'z') ||
          (text[0] >= 'A' && text[0] <= 'Z') || text[0] == '_')) {
        return false;
    }
    for (size_t index = 1; index < length; index++) {
        char c = text[index];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

static bool wl_parse_small_number(const char *text, int minimum, int maximum,
                                  int *out) {
    char *end = NULL;
    long value;

    if (text == NULL || text[0] == '\0') {
        return false;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    if (value < minimum || value > maximum) {
        return false;
    }
    *out = (int)value;
    return true;
}

static int wl_builtin_cd(struct wl_shell *sh, int argc, char **argv) {
    const char *target;
    char old_pwd[WL_PATH_MAX];
    char new_pwd[WL_PATH_MAX];
    bool print_new = false;

    if (argc > 2) {
        fprintf(stderr, "wonderland: cd: too many arguments\n");
        return 1;
    }

    if (argc == 2) {
        if (strcmp(argv[1], "-") == 0) {
            target = wl_var_get(&sh->vars, "OLDPWD", 6);
            if (target == NULL || target[0] == '\0') {
                fprintf(stderr, "wonderland: cd: OLDPWD not set\n");
                return 1;
            }
            print_new = true;
        } else {
            target = argv[1];
        }
    } else {
        target = wl_var_get(&sh->vars, "HOME", 4);
        if (target == NULL || target[0] == '\0') {
            fprintf(stderr, "wonderland: cd: HOME not set\n");
            return 1;
        }
    }

    old_pwd[0] = '\0';
    (void)getcwd(old_pwd, sizeof old_pwd);

    if (chdir(target) != 0) {
        fprintf(stderr, "wonderland: cd: %s: %s\n", target, strerror(errno));
        return 1;
    }

    if (getcwd(new_pwd, sizeof new_pwd) == NULL) {
        fprintf(stderr, "wonderland: cd: %s\n", strerror(errno));
        return 1;
    }

    if (print_new) {
        printf("%s\n", new_pwd);
    }

    if (old_pwd[0] != '\0') {
        (void)wl_var_set(&sh->vars, "OLDPWD", 6, old_pwd, strlen(old_pwd),
                         true);
    }
    if (!wl_var_set(&sh->vars, "PWD", 3, new_pwd, strlen(new_pwd), true)) {
        fprintf(stderr, "wonderland: cd: out of memory\n");
        return 1;
    }
    return 0;
}

static int wl_builtin_exit(struct wl_shell *sh, int argc, char **argv) {
    int status = sh->last_status;

    if (argc > 2) {
        fprintf(stderr, "wonderland: exit: too many arguments\n");
        return 1; /* do not exit: the argument list is ambiguous */
    }

    if (argc == 2 && !wl_parse_small_number(argv[1], 0, 255, &status)) {
        fprintf(stderr, "wonderland: exit: %s: numeric argument required\n",
                argv[1]);
        wl_shell_stop(sh, 2);
        return 2;
    }

    wl_shell_stop(sh, status);
    return status;
}

static int wl_builtin_export(struct wl_shell *sh, int argc, char **argv) {
    int status = 0;

    if (argc == 1) {
        for (struct wl_var *variable = wl_vars_first(&sh->vars);
             variable != NULL; variable = variable->next) {
            if (variable->exported) {
                printf("export %s=%s\n", variable->name, variable->value);
            }
        }
        return 0;
    }

    for (int index = 1; index < argc; index++) {
        const char *argument = argv[index];
        const char *equal = strchr(argument, '=');
        size_t name_len = (equal != NULL) ? (size_t)(equal - argument)
                                          : strlen(argument);

        if (!wl_is_name(argument, name_len)) {
            fprintf(stderr, "wonderland: export: %.*s: not a valid identifier\n",
                    (int)name_len, argument);
            status = 1;
            continue;
        }
        if (equal != NULL) {
            if (!wl_var_set(&sh->vars, argument, name_len, equal + 1,
                            strlen(equal + 1), true)) {
                fprintf(stderr, "wonderland: export: out of memory\n");
                status = 1;
            }
        } else if (!wl_var_export(&sh->vars, argument, name_len)) {
            fprintf(stderr, "wonderland: export: out of memory\n");
            status = 1;
        }
    }
    return status;
}

static int wl_builtin_unset(struct wl_shell *sh, int argc, char **argv) {
    int status = 0;

    for (int index = 1; index < argc; index++) {
        if (!wl_is_name(argv[index], strlen(argv[index]))) {
            fprintf(stderr, "wonderland: unset: %s: not a valid identifier\n",
                    argv[index]);
            status = 1;
            continue;
        }
        (void)wl_var_unset(&sh->vars, argv[index], strlen(argv[index]));
    }
    return status;
}

static int wl_builtin_shift(struct wl_shell *sh, int argc, char **argv) {
    int count = 1;
    size_t available = (sh->frame != NULL) ? sh->frame->count : 0;

    if (argc > 2) {
        fprintf(stderr, "wonderland: shift: too many arguments\n");
        return 1;
    }
    if (argc == 2 &&
        (!wl_parse_small_number(argv[1], 0, 1000000, &count))) {
        fprintf(stderr, "wonderland: shift: %s: numeric argument required\n",
                argv[1]);
        return 1;
    }
    if ((size_t)count > available) {
        fprintf(stderr, "wonderland: shift: cannot shift %d (only %zu)\n",
                count, available);
        return 1;
    }
    wl_shell_shift(sh, (size_t)count);
    return 0;
}

static int wl_builtin_set(struct wl_shell *sh, int argc, char **argv) {
    int index = 1;

    if (argc == 1) {
        for (struct wl_var *variable = wl_vars_first(&sh->vars);
             variable != NULL; variable = variable->next) {
            printf("%s=%s\n", variable->name, variable->value);
        }
        return 0;
    }

    while (index < argc) {
        const char *argument = argv[index];

        if (strcmp(argument, "--") == 0) {
            index++;
            if (!wl_shell_set_args(sh, sh->frame->name0, &argv[index],
                                   (size_t)(argc - index))) {
                fprintf(stderr, "wonderland: set: out of memory\n");
                return 1;
            }
            return 0;
        }
        if (argument[0] == '-' || argument[0] == '+') {
            bool enable = argument[0] == '-';

            if (argument[1] == '\0') {
                fprintf(stderr, "wonderland: set: use -- for positional "
                                "parameters\n");
                return 2;
            }
            for (const char *flag = argument + 1; *flag != '\0'; flag++) {
                switch (*flag) {
                case 'e':
                    sh->errexit = enable;
                    break;
                case 'u':
                    sh->nounset = enable;
                    break;
                case 'x':
                    sh->xtrace = enable;
                    break;
                case 'n':
                    sh->noexec = enable;
                    break;
                case 'f':
                    /* noglob: accepted but not implemented until M8. */
                    break;
                default:
                    fprintf(stderr,
                            "wonderland: set: unsupported option -%c\n",
                            *flag);
                    return 2;
                }
            }
            index++;
            continue;
        }
        fprintf(stderr, "wonderland: set: use -- before positional "
                        "parameters\n");
        return 2;
    }
    return 0;
}

static int wl_builtin_return(struct wl_shell *sh, int argc, char **argv) {
    int status = sh->last_status;

    if (argc > 2) {
        fprintf(stderr, "wonderland: return: too many arguments\n");
        return 1;
    }
    if (argc == 2 && !wl_parse_small_number(argv[1], 0, 255, &status)) {
        fprintf(stderr, "wonderland: return: %s: numeric argument required\n",
                argv[1]);
        return 2;
    }
    if (sh->function_depth == 0) {
        fprintf(stderr, "wonderland: return: not in a function\n");
        return 1;
    }
    sh->returning = true;
    sh->return_status = status;
    return status;
}

static int wl_builtin_break(struct wl_shell *sh, int argc, char **argv) {
    int count = 1;

    if (argc > 2) {
        fprintf(stderr, "wonderland: break: too many arguments\n");
        return 1;
    }
    if (argc == 2 && !wl_parse_small_number(argv[1], 1, 1000000, &count)) {
        fprintf(stderr, "wonderland: break: %s: numeric argument required\n",
                argv[1]);
        return 1;
    }
    if (sh->loop_depth == 0) {
        fprintf(stderr, "wonderland: break: not in a loop\n");
        return 1;
    }
    sh->break_count = count;
    return 0;
}

static int wl_builtin_continue(struct wl_shell *sh, int argc, char **argv) {
    int count = 1;

    if (argc > 2) {
        fprintf(stderr, "wonderland: continue: too many arguments\n");
        return 1;
    }
    if (argc == 2 && !wl_parse_small_number(argv[1], 1, 1000000, &count)) {
        fprintf(stderr, "wonderland: continue: %s: numeric argument "
                        "required\n",
                argv[1]);
        return 1;
    }
    if (sh->loop_depth == 0) {
        fprintf(stderr, "wonderland: continue: not in a loop\n");
        return 1;
    }
    sh->continue_count = count;
    return 0;
}

static char **wl_trap_slot(struct wl_shell *sh, const char *name, int *signo) {
    *signo = 0;
    if (strcmp(name, "EXIT") == 0 || strcmp(name, "0") == 0) {
        return &sh->trap_exit;
    }
    if (strcmp(name, "INT") == 0 || strcmp(name, "SIGINT") == 0) {
        *signo = SIGINT;
        return &sh->trap_int;
    }
    if (strcmp(name, "TERM") == 0 || strcmp(name, "SIGTERM") == 0) {
        *signo = SIGTERM;
        return &sh->trap_term;
    }
    return NULL;
}

static int wl_builtin_trap(struct wl_shell *sh, int argc, char **argv) {
    int status = 0;

    if (argc == 1) {
        if (sh->trap_exit != NULL) {
            printf("trap -- '%s' EXIT\n", sh->trap_exit);
        }
        if (sh->trap_int != NULL) {
            printf("trap -- '%s' INT\n", sh->trap_int);
        }
        if (sh->trap_term != NULL) {
            printf("trap -- '%s' TERM\n", sh->trap_term);
        }
        return 0;
    }
    if (argc == 2) {
        fprintf(stderr, "wonderland: trap: missing signal operands\n");
        return 2;
    }

    {
        const char *action = argv[1];
        bool reset = strcmp(action, "-") == 0;

        for (int index = 2; index < argc; index++) {
            int signo = 0;
            char **slot = wl_trap_slot(sh, argv[index], &signo);

            if (slot == NULL) {
                fprintf(stderr,
                        "wonderland: trap: unsupported signal: %s\n",
                        argv[index]);
                status = 1;
                continue;
            }
            free(*slot);
            if (reset) {
                *slot = NULL;
            } else {
                *slot = strdup(action);
                if (*slot == NULL) {
                    fprintf(stderr, "wonderland: trap: out of memory\n");
                    status = 1;
                }
            }
            if (signo != 0 && !reset) {
                wl_signal_install_trap(signo);
            }
        }
    }
    return status;
}

static struct wl_job *wl_resolve_job(struct wl_shell *sh, const char *arg) {
    if (arg == NULL) {
        return wl_job_current(&sh->jobs);
    }
    if (arg[0] == '%') {
        if (arg[1] == '\0' || (arg[1] == '%' && arg[2] == '\0') ||
            (arg[1] == '+' && arg[2] == '\0')) {
            return wl_job_current(&sh->jobs);
        }
        if (arg[1] == '-' && arg[2] == '\0') {
            return wl_job_previous(&sh->jobs);
        }
        if (arg[1] >= '0' && arg[1] <= '9') {
            return wl_job_find_id(&sh->jobs, atoi(arg + 1));
        }
        return NULL;
    }
    {
        char *end = NULL;
        long pid = strtol(arg, &end, 10);

        if (end != arg && *end == '\0') {
            return wl_job_find_pid(&sh->jobs, (pid_t)pid);
        }
    }
    return NULL;
}

static int wl_builtin_jobs(struct wl_shell *sh, int argc, char **argv) {
    (void)argc;
    (void)argv;

    wl_job_notify(sh);
    for (struct wl_job *job = sh->jobs.head; job != NULL; job = job->next) {
        const char *state = "Running";

        if (job->state == WL_JOB_STOPPED) {
            state = "Stopped";
        } else if (job->state == WL_JOB_DONE) {
            state = "Done";
        }
        printf("[%d] %-9s %s\n", job->id, state, job->command);
    }
    return 0;
}

static int wl_builtin_fg(struct wl_shell *sh, int argc, char **argv) {
    struct wl_job *job;

    if (argc > 2) {
        fprintf(stderr, "wonderland: fg: too many arguments\n");
        return 1;
    }
    wl_job_notify(sh);
    job = wl_resolve_job(sh, (argc == 2) ? argv[1] : NULL);
    if (job == NULL) {
        fprintf(stderr, "wonderland: fg: no such job\n");
        return 1;
    }
    return wl_job_foreground(sh, job);
}

static int wl_builtin_bg(struct wl_shell *sh, int argc, char **argv) {
    struct wl_job *job;

    if (argc > 2) {
        fprintf(stderr, "wonderland: bg: too many arguments\n");
        return 1;
    }
    wl_job_notify(sh);
    job = wl_resolve_job(sh, (argc == 2) ? argv[1] : NULL);
    if (job == NULL) {
        fprintf(stderr, "wonderland: bg: no such job\n");
        return 1;
    }
    if (job->state == WL_JOB_DONE) {
        fprintf(stderr, "wonderland: bg: job %d has finished\n", job->id);
        return 1;
    }
    wl_job_background(sh, job);
    return 0;
}

static int wl_builtin_wait(struct wl_shell *sh, int argc, char **argv) {
    int status = 0;

    wl_job_notify(sh);

    if (argc == 1) {
        while (sh->jobs.head != NULL) {
            struct wl_job *job = sh->jobs.head;
            bool stopped = false;

            status = wl_job_wait(sh, job, &stopped);
            if (stopped) {
                job->state = WL_JOB_STOPPED;
                job->notified = sh->interactive;
                break;
            }
            wl_job_remove(&sh->jobs, job);
        }
        return status;
    }

    for (int index = 1; index < argc; index++) {
        struct wl_job *job = wl_resolve_job(sh, argv[index]);
        bool stopped = false;

        if (job == NULL) {
            fprintf(stderr, "wonderland: wait: %s: no such job\n", argv[index]);
            status = 127;
            continue;
        }
        status = wl_job_wait(sh, job, &stopped);
        if (stopped) {
            job->state = WL_JOB_STOPPED;
            job->notified = sh->interactive;
        } else {
            wl_job_remove(&sh->jobs, job);
        }
    }
    return status;
}

static int wl_builtin_read(struct wl_shell *sh, int argc, char **argv) {
    bool raw = false;
    int index = 1;
    char line[WL_LINE_MAX];
    size_t length = 0;
    enum wl_line_status read_status;

    while (index < argc && argv[index][0] == '-' && argv[index][1] != '\0') {
        if (strcmp(argv[index], "-r") == 0) {
            raw = true;
            index++;
            continue;
        }
        fprintf(stderr, "wonderland: read: unknown option '%s'\n",
                argv[index]);
        return 2;
    }

    read_status = wl_line_read(STDIN_FILENO, line, sizeof line, &length);
    if (read_status != WL_LINE_OK) {
        return 1;
    }

    if (!raw) {
        size_t out = 0;

        for (size_t at = 0; at < length; at++) {
            if (line[at] == '\\' && at + 1 < length) {
                at++;
                line[out++] = line[at];
            } else {
                line[out++] = line[at];
            }
        }
        length = out;
        line[length] = '\0';
    }

    {
        int names = argc - index;

        if (names <= 0) {
            if (!wl_var_set(&sh->vars, "REPLY", 5, line, length, false)) {
                fprintf(stderr, "wonderland: read: out of memory\n");
                return 1;
            }
            return 0;
        }

        {
            const char *ifs = wl_var_get(&sh->vars, "IFS", 3);
            size_t position = 0;

            if (ifs == NULL) {
                ifs = " \t\n";
            }

            for (int field = 0; field < names; field++) {
                size_t start;
                size_t end;

                while (position < length &&
                       strchr(ifs, line[position]) != NULL) {
                    position++;
                }
                start = position;
                if (field == names - 1) {
                    position = length;
                } else {
                    while (position < length &&
                           strchr(ifs, line[position]) == NULL) {
                        position++;
                    }
                }
                end = position;
                if (field == names - 1) {
                    while (end > start && strchr(ifs, line[end - 1]) != NULL) {
                        end--;
                    }
                }
                if (!wl_var_set(&sh->vars, argv[index + field],
                                strlen(argv[index + field]), line + start,
                                end - start, false)) {
                    fprintf(stderr, "wonderland: read: out of memory\n");
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int wl_builtin_colon(struct wl_shell *sh, int argc, char **argv) {
    (void)sh;
    (void)argc;
    (void)argv;
    return 0;
}

struct wl_builtin {
    const char *name;
    int (*run)(struct wl_shell *sh, int argc, char **argv);
};

static const struct wl_builtin wl_builtins[] = {
    {":", wl_builtin_colon},
    {"bg", wl_builtin_bg},
    {"break", wl_builtin_break},
    {"cd", wl_builtin_cd},
    {"continue", wl_builtin_continue},
    {"exit", wl_builtin_exit},
    {"export", wl_builtin_export},
    {"fg", wl_builtin_fg},
    {"jobs", wl_builtin_jobs},
    {"read", wl_builtin_read},
    {"return", wl_builtin_return},
    {"set", wl_builtin_set},
    {"shift", wl_builtin_shift},
    {"trap", wl_builtin_trap},
    {"unset", wl_builtin_unset},
    {"wait", wl_builtin_wait},
};

bool wl_builtin_is(const char *name) {
    size_t count = sizeof wl_builtins / sizeof wl_builtins[0];

    for (size_t index = 0; index < count; index++) {
        if (strcmp(name, wl_builtins[index].name) == 0) {
            return true;
        }
    }
    return false;
}

bool wl_builtin_run(struct wl_shell *sh, int argc, char **argv) {
    size_t count = sizeof wl_builtins / sizeof wl_builtins[0];

    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        return false;
    }
    for (size_t index = 0; index < count; index++) {
        if (strcmp(argv[0], wl_builtins[index].name) == 0) {
            int status = wl_builtins[index].run(sh, argc, argv);

            wl_shell_set_status(sh, status);
            return true;
        }
    }
    return false;
}
