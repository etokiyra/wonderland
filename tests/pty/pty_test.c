/*
 * Pseudo-terminal test for the interactive shell.
 *
 * forkpty gives the shell a real terminal, which is the only way to observe
 * the prompt, Ctrl-C handling, and terminal settings honestly. No expect, no
 * shell script: a small C driver with a bounded wait.
 *
 * Terminal echo is switched off after the pty is created so that assertions
 * see program output only. The shell does not read or write termios beyond
 * saving it, so this changes nothing about what is being tested, and the
 * "settings unchanged" check uses the post-setup baseline.
 *
 * Exit status: 0 all checks passed, 1 otherwise.
 */

#define _DEFAULT_SOURCE

#include <errno.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define WL_OUTPUT_CAP 32768

static int wl_master = -1;
static char wl_output[WL_OUTPUT_CAP];
static size_t wl_output_len;
static int wl_failures;

static void wl_fail(const char *message) {
    fprintf(stderr, "pty: FAIL: %s\n", message);
    wl_failures++;
}

/* Returns 1 on data, 0 on timeout, -1 on EOF/error. */
static int wl_pump(int timeout_ms) {
    struct pollfd pfd;
    char chunk[1024];
    ssize_t count;
    int ready;

    pfd.fd = wl_master;
    pfd.events = POLLIN;
    pfd.revents = 0;
    ready = poll(&pfd, 1, timeout_ms);
    if (ready <= 0) {
        return ready;
    }

    count = read(wl_master, chunk, sizeof chunk);
    if (count <= 0) {
        return -1;
    }
    if (wl_output_len + (size_t)count < WL_OUTPUT_CAP) {
        memcpy(wl_output + wl_output_len, chunk, (size_t)count);
        wl_output_len += (size_t)count;
        wl_output[wl_output_len] = '\0';
    }
    return 1;
}

static int wl_wait_for(const char *needle, int timeout_ms) {
    int waited = 0;

    while (waited < timeout_ms) {
        if (strstr(wl_output, needle) != NULL) {
            return 1;
        }
        if (wl_pump(50) < 0) {
            break;
        }
        waited += 50;
    }
    return strstr(wl_output, needle) != NULL;
}

static int wl_count_of(const char *needle) {
    const char *cursor = wl_output;
    int count = 0;
    size_t length = strlen(needle);

    while ((cursor = strstr(cursor, needle)) != NULL) {
        count++;
        cursor += length;
    }
    return count;
}

static int wl_wait_for_count(const char *needle, int wanted, int timeout_ms) {
    int waited = 0;

    while (waited < timeout_ms) {
        if (wl_count_of(needle) >= wanted) {
            return 1;
        }
        if (wl_pump(50) < 0) {
            break;
        }
        waited += 50;
    }
    return wl_count_of(needle) >= wanted;
}

static int wl_send(const char *text) {
    size_t length = strlen(text);
    size_t sent = 0;

    while (sent < length) {
        ssize_t count = write(wl_master, text + sent, length - sent);

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        sent += (size_t)count;
    }
    return 1;
}

static int wl_termios_equal(const struct termios *left,
                            const struct termios *right) {
    return left->c_iflag == right->c_iflag &&
           left->c_oflag == right->c_oflag &&
           left->c_cflag == right->c_cflag &&
           left->c_lflag == right->c_lflag &&
           memcmp(left->c_cc, right->c_cc, sizeof left->c_cc) == 0;
}

/*
 * Each completed command produces exactly one new prompt. Waiting on the count
 * instead of on output text avoids depending on timing or on echoed input.
 */
/*
 * A completed command prints "
" and the editor then renders "
" plus the
 * prompt. Counting that sequence ignores the per-keystroke redraws, which also
 * contain the prompt text.
 */
static int wl_next_prompt(int *prompts, const char *label) {
    (*prompts)++;
    if (!wl_wait_for_count("\r\n\rwonderland:", *prompts, 5000)) {
        wl_fail(label);
        return 0;
    }
    return 1;
}

static void wl_disable_echo(void) {
    struct termios settings;

    if (tcgetattr(wl_master, &settings) != 0) {
        wl_fail("could not read terminal settings");
        return;
    }
    settings.c_lflag &= (tcflag_t)~ECHO;
    if (tcsetattr(wl_master, TCSANOW, &settings) != 0) {
        wl_fail("could not disable terminal echo");
    }
}

/*
 * A second, short session with --plain: a failing command must produce no
 * narrative text at all.
 */
static void wl_plain_session(const char *binary) {
    pid_t child;
    int status = 0;
    int prompts;
    int waited;

    wl_master = -1;
    wl_output_len = 0;
    wl_output[0] = '\0';
    wl_failures = 0;

    child = forkpty(&wl_master, NULL, NULL, NULL);
    if (child < 0) {
        wl_fail("plain: forkpty failed");
        return;
    }
    if (child == 0) {
        execl(binary, binary, "--plain", (char *)NULL);
        _exit(127);
    }

    wl_disable_echo();
    if (!wl_wait_for("wonderland:", 5000)) {
        wl_fail("plain: prompt did not appear");
    }
    prompts = wl_count_of("\r\n\rwonderland:");
    if (!wl_send("false\n")) {
        wl_fail("plain: could not write");
    }
    if (!wl_wait_for_count("\r\n\rwonderland:", prompts + 1, 5000)) {
        wl_fail("plain: no prompt after false");
    }
    if (strstr(wl_output, "refused you") != NULL ||
        strstr(wl_output, "house is particular") != NULL) {
        wl_fail("plain mode produced narrative text");
    }
    if (!wl_send("exit 0\n")) {
        wl_fail("plain: could not write exit");
    }
    for (waited = 0; waited < 5000; waited += 50) {
        pid_t reaped = waitpid(child, &status, WNOHANG);

        if (reaped == child) {
            break;
        }
        if (reaped < 0) {
            break;
        }
        (void)wl_pump(50);
    }
}

int main(int argc, char **argv) {
    const char *binary = (argc > 1) ? argv[1] : "build/wonderland";
    struct termios baseline;
    struct termios at_prompt;
    int prompts = 0;
    int status = 0;
    int reaped = 0;
    pid_t child;

    child = forkpty(&wl_master, NULL, NULL, NULL);
    if (child < 0) {
        perror("pty: forkpty");
        return 1;
    }
    if (child == 0) {
        execl(binary, binary, (char *)NULL);
        _exit(127);
    }

    wl_disable_echo();
    if (tcgetattr(wl_master, &baseline) != 0) {
        wl_fail("could not read baseline terminal settings");
    }

    if (!wl_wait_for("wonderland:", 5000)) {
        wl_fail("prompt did not appear");
    }
    prompts = 0;

    if (!wl_send("echo hello\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after echo") &&
        !wl_wait_for("hello\r\n", 2000)) {
        wl_fail("echo did not run");
    }

    /* History recall: the newest entry is "echo hello". */
    if (!wl_send("\033[A\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after history recall") &&
        !wl_wait_for("hello\r\n", 2000)) {
        wl_fail("Up arrow did not recall the last command");
    }

    /* Ctrl-A then insertion, building "echo hello world". */
    if (!wl_send("world") || !wl_send("\001") ||
        !wl_send("echo hello ") || !wl_send("\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after Ctrl-A editing") &&
        !wl_wait_for("hello world\r\n", 2000)) {
        wl_fail("Ctrl-A and insertion produced the wrong line");
    }

    /* Ctrl-U clears the pending line. */
    if (!wl_send("junk") || !wl_send("\025") || !wl_send("echo clean\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after Ctrl-U") &&
        !wl_wait_for("clean\r\n", 2000)) {
        wl_fail("Ctrl-U did not clear the line");
    }

    /* Tab completes "ech" to "echo". */
    if (!wl_send("ech\t hi\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after completion") &&
        !wl_wait_for("hi\r\n", 2000)) {
        wl_fail("tab completion did not complete the command");
    }

    /* A trailing backslash continues the logical line (M2 continuation). */
    if (!wl_send("echo ab\\\n")) {
        wl_fail("could not write to the terminal");
    }
    if (!wl_wait_for("> ", 2000)) {
        wl_fail("no continuation prompt for a trailing backslash");
    }
    if (!wl_send("cd\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after the continued command") &&
        !wl_wait_for("abcd\r\n", 2000)) {
        wl_fail("continuation did not complete the command");
    }

    /* An open single quote also continues. */
    if (!wl_send("echo 'x\n")) {
        wl_fail("could not write to the terminal");
    }
    if (!wl_wait_for("> ", 2000)) {
        wl_fail("no continuation prompt for an open quote");
    }
    if (!wl_send("y'\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after the quoted continuation") &&
        !wl_wait_for("x\r\ny\r\n", 2000)) {
        wl_fail("quoted continuation did not preserve the newline");
    }

    if (!wl_send("cd /tmp\n")) {
        wl_fail("could not write to the terminal");
    }
    (void)wl_next_prompt(&prompts, "no prompt after cd");

    if (!wl_send("pwd\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after pwd") &&
        !wl_wait_for("/tmp\r\n", 2000)) {
        wl_fail("cd did not change the working directory");
    }

    /* A pipeline through a real terminal. */
    if (!wl_send("printf 'a\\nb\\n' | wc -l\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after the pipeline") &&
        !wl_wait_for("2\r\n", 2000)) {
        wl_fail("pipeline produced the wrong output");
    }

    /* A here-document typed at the prompt. */
    if (!wl_send("cat <<EOF\nheredoc body\nEOF\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after the here-document") &&
        !wl_wait_for("heredoc body\r\n", 2000)) {
        wl_fail("here-document produced the wrong output");
    }

    /* Background jobs: start, list, wait. */
    if (!wl_send("sleep 2 &\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after starting a background job") &&
        !wl_wait_for("[1]", 2000)) {
        wl_fail("background job did not announce itself");
    }
    if (!wl_send("jobs\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after jobs") &&
        !wl_wait_for("Running", 2000)) {
        wl_fail("jobs did not list the background job");
    }
    if (!wl_send("wait\n")) {
        wl_fail("could not write to the terminal");
    }
    (void)wl_next_prompt(&prompts, "no prompt after wait");

    /* Ctrl-Z stops a foreground job; bg resumes it in the background. */
    if (!wl_send("sleep 0.5\n")) {
        wl_fail("could not write to the terminal");
    }
    (void)usleep(150000);
    if (!wl_send("\032")) {
        wl_fail("could not send Ctrl-Z");
    }
    if (!wl_wait_for("Stopped", 2000)) {
        wl_fail("Ctrl-Z did not stop the job");
    }
    (void)wl_next_prompt(&prompts, "no prompt after Ctrl-Z");
    if (!wl_send("jobs\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after jobs (stopped)") &&
        !wl_wait_for("Stopped", 2000)) {
        wl_fail("jobs did not list the stopped job");
    }
    if (!wl_send("bg\n")) {
        wl_fail("could not write to the terminal");
    }
    (void)wl_next_prompt(&prompts, "no prompt after bg");
    if (!wl_send("wait\n")) {
        wl_fail("could not write to the terminal");
    }
    (void)wl_next_prompt(&prompts, "no prompt after waiting for the resumed job");

    /* Ctrl-C at the prompt must return to a fresh prompt, not kill the shell. */
    if (!wl_send("abc")) {
        wl_fail("could not write to the terminal");
    }
    (void)usleep(50000);
    if (!wl_send("\003")) {
        wl_fail("could not send Ctrl-C");
    }
    (void)wl_next_prompt(&prompts, "Ctrl-C did not return to a prompt");

    if (!wl_send("echo after-interrupt\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after the Ctrl-C echo") &&
        !wl_wait_for("after-interrupt\r\n", 2000)) {
        wl_fail("shell unusable after Ctrl-C");
    }

    /* The narrative slice speaks on failure (first failure is deterministic). */
    if (!wl_send("false\n")) {
        wl_fail("could not write to the terminal");
    }
    if (wl_next_prompt(&prompts, "no prompt after false") &&
        !wl_wait_for("Something refused you", 2000)) {
        wl_fail("narrative slice did not speak on failure");
    }

    /*
     * At the prompt the editor owns the terminal and keeps it raw; while a
     * foreground command runs it must be back to the saved settings.
     */
    if (tcgetattr(wl_master, &at_prompt) != 0) {
        wl_fail("could not read terminal settings at the prompt");
    } else if ((at_prompt.c_lflag & (ECHO | ICANON)) != 0) {
        wl_fail("prompt is not in raw mode");
    }

    if (!wl_send("sleep 0.3\n")) {
        wl_fail("could not write to the terminal");
    }
    (void)usleep(100000);
    {
        struct termios during;

        if (tcgetattr(wl_master, &during) != 0) {
            wl_fail("could not read terminal settings during a command");
        } else if (!wl_termios_equal(&baseline, &during)) {
            wl_fail("terminal settings not restored for a foreground command");
        }
    }
    (void)wl_next_prompt(&prompts, "no prompt after the restore check");

    if (!wl_send("exit 4\n")) {
        wl_fail("could not write to the terminal");
    }
    for (int waited = 0; waited < 5000; waited += 50) {
        pid_t result = waitpid(child, &status, WNOHANG);

        if (result == child) {
            reaped = 1;
            break;
        }
        if (result < 0) {
            break;
        }
        (void)wl_pump(50);
    }
    if (!reaped) {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, &status, 0);
        wl_fail("shell did not exit after `exit 4`");
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 4) {
        wl_fail("exit status was not 4");
    }

    if (wl_failures != 0) {
        fprintf(stderr, "pty: FAIL (%d)\n--- session ---\n%s\n", wl_failures,
                wl_output);
        return 1;
    }

    wl_plain_session(binary);
    if (wl_failures != 0) {
        fprintf(stderr, "pty: FAIL (%d in the plain session)\n%s\n",
                wl_failures, wl_output);
        return 1;
    }
    printf("pty: ok (prompt, command, cd, pipeline, here-document, jobs, "
           "Ctrl-C, Ctrl-Z, bg, narrative, plain, exit 4, termios "
           "unchanged)\n");
    return 0;
}
