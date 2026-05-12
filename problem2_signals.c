#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHILD_COUNT 4
#define BASE_SIGNAL_COUNT 8
#define Q3_SIGNAL_COUNT 9

static const int base_signals[BASE_SIGNAL_COUNT] = {
    SIGINT, SIGABRT, SIGILL, SIGCHLD, SIGSEGV, SIGFPE, SIGHUP, SIGTSTP
};

static const int q3_signals[Q3_SIGNAL_COUNT] = {
    SIGINT, SIGQUIT, SIGTSTP, SIGABRT, SIGILL, SIGCHLD, SIGSEGV, SIGFPE, SIGHUP
};

typedef struct {
    pid_t target_child;
    pid_t parent_pid;
    int repeats;
} Q2Instruction;

static int g_fast = 0;

static const char *signal_name(int signo) {
    switch (signo) {
        case SIGABRT: return "SIGABRT";
        case SIGCHLD: return "SIGCHLD";
        case SIGFPE: return "SIGFPE";
        case SIGHUP: return "SIGHUP";
        case SIGILL: return "SIGILL";
        case SIGINT: return "SIGINT";
        case SIGQUIT: return "SIGQUIT";
        case SIGSEGV: return "SIGSEGV";
        case SIGTSTP: return "SIGTSTP";
        default: return "UNKNOWN";
    }
}

static int contains_signal(const int *signals, int count, int signo) {
    for (int i = 0; i < count; i++) {
        if (signals[i] == signo) {
            return 1;
        }
    }
    return 0;
}

static void signal_info_handler(int signo, siginfo_t *info, void *context) {
    (void)context;

    printf("[handler] signal=%d (%s), recipient pid=%ld, parent pid=%ld, sender pid=%ld\n",
           signo, signal_name(signo), (long)getpid(), (long)getppid(),
           (info != NULL) ? (long)info->si_pid : -1L);
}

static void install_one_handler(int signo, const sigset_t *extra_mask) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = signal_info_handler;
    sa.sa_flags = SA_SIGINFO;
    if (extra_mask != NULL) {
        sa.sa_mask = *extra_mask;
    } else {
        sigemptyset(&sa.sa_mask);
    }

    if (sigaction(signo, &sa, NULL) == -1) {
        perror("sigaction handler");
    }
}

static void ignore_one_signal(int signo) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    if (sigaction(signo, &sa, NULL) == -1) {
        perror("sigaction ignore");
    }
}

static void default_one_signal(int signo) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    if (sigaction(signo, &sa, NULL) == -1) {
        perror("sigaction default");
    }
}

static void install_handlers_for_list(const int *signals, int count) {
    sigset_t empty;
    sigemptyset(&empty);
    for (int i = 0; i < count; i++) {
        install_one_handler(signals[i], &empty);
    }
}

static void ignore_list(const int *signals, int count) {
    for (int i = 0; i < count; i++) {
        ignore_one_signal(signals[i]);
    }
}

static void restore_defaults_for_list(const int *signals, int count) {
    for (int i = 0; i < count; i++) {
        default_one_signal(signals[i]);
    }
}

static void set_process_mask(const int *signals, int count) {
    sigset_t mask;
    sigemptyset(&mask);

    for (int i = 0; i < count; i++) {
        sigaddset(&mask, signals[i]);
    }

    if (sigprocmask(SIG_SETMASK, &mask, NULL) == -1) {
        perror("sigprocmask SIG_SETMASK");
    }
}

static void print_signal_list(const char *label, const int *signals, int count) {
    char line[512];
    int used = snprintf(line, sizeof(line), "%s", label);

    for (int i = 0; i < count; i++) {
        if (used < (int)sizeof(line)) {
            used += snprintf(line + used, sizeof(line) - (size_t)used,
                             " %s", signal_name(signals[i]));
        }
    }
    if (used < (int)sizeof(line) - 1) {
        line[used++] = '\n';
        line[used] = '\0';
    }
    if (write(STDOUT_FILENO, line, strlen(line)) == -1) {
        perror("write signal list");
    }
}

static void print_pending_queue(const char *label, const int *signals, int count) {
    sigset_t pending;
    char line[512];
    int used;

    if (sigpending(&pending) == -1) {
        perror("sigpending");
        return;
    }

    used = snprintf(line, sizeof(line), "%s pid=%ld pending:", label, (long)getpid());
    for (int i = 0; i < count; i++) {
        if (sigismember(&pending, signals[i])) {
            if (used < (int)sizeof(line)) {
                used += snprintf(line + used, sizeof(line) - (size_t)used,
                                 " %s", signal_name(signals[i]));
            }
        }
    }
    if (used < (int)sizeof(line) - 1) {
        line[used++] = '\n';
        line[used] = '\0';
    }
    if (write(STDOUT_FILENO, line, strlen(line)) == -1) {
        perror("write pending queue");
    }
}

static void sleep_all(unsigned int seconds) {
    struct timespec req;
    struct timespec rem;

    req.tv_sec = seconds;
    req.tv_nsec = 0;

    while (nanosleep(&req, &rem) == -1) {
        if (errno != EINTR) {
            perror("nanosleep");
            return;
        }
        req = rem;
    }
}

static void consume_pending_examples(const char *label, const int *blocked, int count) {
    sigset_t waitset;
    siginfo_t info;
    struct timespec timeout;
    int signo;

    sigemptyset(&waitset);
    for (int i = 0; i < count; i++) {
        sigaddset(&waitset, blocked[i]);
    }

    if (count <= 0) {
        printf("%s pid=%ld has no blocked set to test with sigwait\n", label, (long)getpid());
        return;
    }

    signo = 0;
    if (sigwait(&waitset, &signo) == 0) {
        printf("%s sigwait consumed %d (%s) in pid=%ld\n",
               label, signo, signal_name(signo), (long)getpid());
    } else {
        perror("sigwait");
    }

    memset(&info, 0, sizeof(info));
    signo = sigwaitinfo(&waitset, &info);
    if (signo >= 0) {
        printf("%s sigwaitinfo consumed %d (%s) in pid=%ld from sender=%ld\n",
               label, signo, signal_name(signo), (long)getpid(), (long)info.si_pid);
    } else {
        printf("%s sigwaitinfo found no signal immediately or was interrupted: errno=%d\n",
               label, errno);
    }

    memset(&info, 0, sizeof(info));
    timeout.tv_sec = 0;
    timeout.tv_nsec = 100000000L;
    signo = sigtimedwait(&waitset, &info, &timeout);
    if (signo >= 0) {
        printf("%s sigtimedwait consumed %d (%s) in pid=%ld from sender=%ld\n",
               label, signo, signal_name(signo), (long)getpid(), (long)info.si_pid);
    } else if (errno == EAGAIN) {
        printf("%s sigtimedwait timed out, showing standard signals do not queue multiple copies here\n",
               label);
    } else {
        printf("%s sigtimedwait errno=%d\n", label, errno);
    }
}

static void wait_for_children(int count) {
    int finished = 0;

    while (finished < count) {
        int status;
        pid_t w = waitpid(-1, &status, 0);

        if (w == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("waitpid");
            break;
        }

        finished++;
        if (WIFEXITED(status)) {
            printf("Parent pid=%ld reaped child pid=%ld WIFEXITED status=%d\n",
                   (long)getpid(), (long)w, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Parent pid=%ld reaped child pid=%ld WIFSIGNALED signal=%d (%s)\n",
                   (long)getpid(), (long)w, WTERMSIG(status), signal_name(WTERMSIG(status)));
        } else {
            printf("Parent pid=%ld reaped child pid=%ld with raw status=%d\n",
                   (long)getpid(), (long)w, status);
        }
    }

}

static void configure_child_part1(int child_index) {
    static const int caught[CHILD_COUNT][4] = {
        {SIGINT,  SIGABRT, SIGILL,  SIGCHLD},
        {SIGSEGV, SIGFPE,  SIGHUP,  SIGTSTP},
        {SIGINT,  SIGSEGV, SIGHUP,  SIGILL},
        {SIGABRT, SIGFPE,  SIGCHLD, SIGTSTP}
    };
    static const int always_blocked[CHILD_COUNT][2] = {
        {SIGSEGV, SIGFPE},
        {SIGINT,  SIGABRT},
        {SIGABRT, SIGTSTP},
        {SIGINT,  SIGHUP}
    };

    sigset_t handler_mask;
    sigemptyset(&handler_mask);
    sigaddset(&handler_mask, caught[child_index][2]);
    sigaddset(&handler_mask, caught[child_index][3]);

    for (int i = 0; i < BASE_SIGNAL_COUNT; i++) {
        int signo = base_signals[i];
        if (contains_signal(caught[child_index], 4, signo)) {
            install_one_handler(signo, &handler_mask);
        } else if (contains_signal(always_blocked[child_index], 2, signo)) {
            default_one_signal(signo);
        } else {
            ignore_one_signal(signo);
        }
    }

    set_process_mask(always_blocked[child_index], 2);

    printf("Child index=%d pid=%ld configured signal policy\n", child_index, (long)getpid());
    print_signal_list("  catches:", caught[child_index], 4);
    print_signal_list("  blocked for full execution:", always_blocked[child_index], 2);
    printf("  other listed signals are ignored. Two caught signals are also masked during handlers.\n");
}

static long long compute_sum_with_sleep(int child_index) {
    pid_t pid = getpid();
    long long limit = (long long)pid * 10LL;
    long long sum = 0;
    unsigned int per_iteration_sleep = g_fast ? 0U : 10U;

    printf("child=%d pid=%ld sum 0..%lld sleep=%u\n",
           child_index, (long)pid, limit, per_iteration_sleep);

    if (g_fast) {
        sleep(1);
    }

    for (long long i = 0; i <= limit; i++) {
        sum += i;
        if (!g_fast) {
            sleep(per_iteration_sleep);
        }
    }

    if (g_fast) {
        sleep(1);
    }

    printf("child=%d pid=%ld sum=%lld\n", child_index, (long)pid, sum);
    return sum;
}

static void child_part1_body(int child_index) {
    configure_child_part1(child_index);
    compute_sum_with_sleep(child_index);
    print_pending_queue("Child part1 before exit", base_signals, BASE_SIGNAL_COUNT);
    _exit(20 + child_index);
}

static void run_part1(void) {
    printf("Problem 2 part1 parent pid=%ld initially ignores all eight listed signals while forking children.\n",
           (long)getpid());
    ignore_list(base_signals, BASE_SIGNAL_COUNT);

    for (int i = 0; i < CHILD_COUNT; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        if (pid == 0) {
            child_part1_body(i);
        }
    }

    printf("Parent pid=%ld finished forking; now catches the listed signals while children run.\n",
           (long)getpid());
    install_handlers_for_list(base_signals, BASE_SIGNAL_COUNT);
    wait_for_children(CHILD_COUNT);

    printf("Parent pid=%ld restoring default handlers for the eight listed signals.\n", (long)getpid());
    restore_defaults_for_list(base_signals, BASE_SIGNAL_COUNT);
    printf("Parent pid=%ld sleeping after restoring defaults for %u second(s)\n",
           (long)getpid(), g_fast ? 2U : 20U);
    sleep(g_fast ? 2U : 20U);
    printf("Problem 2 part1 parent completed.\n");
}

static void q2_sender_child(int read_fd) {
    Q2Instruction instr;

    configure_child_part1(0);
    if (read(read_fd, &instr, sizeof(instr)) != (ssize_t)sizeof(instr)) {
        perror("read q2 instruction");
        _exit(2);
    }
    close(read_fd);

    printf("Q2 sender child pid=%ld will send every listed signal %d time(s) to child pid=%ld and parent pid=%ld\n",
           (long)getpid(), instr.repeats, (long)instr.target_child, (long)instr.parent_pid);
    sleep(1);

    for (int i = 0; i < BASE_SIGNAL_COUNT; i++) {
        int signo = base_signals[i];
        for (int r = 0; r < instr.repeats; r++) {
            kill(instr.target_child, signo);
            kill(instr.parent_pid, signo);
        }
    }

    compute_sum_with_sleep(0);
    print_pending_queue("Q2 sender child before exit", base_signals, BASE_SIGNAL_COUNT);
    _exit(40);
}

static void q2_receiver_child(int child_index) {
    configure_child_part1(child_index);
    printf("Q2 receiver child index=%d pid=%ld staying alive so another child can signal it.\n",
           child_index, (long)getpid());
    sleep_all(g_fast ? 5U : 20U);
    compute_sum_with_sleep(child_index);
    print_pending_queue("Q2 receiver child before exit", base_signals, BASE_SIGNAL_COUNT);
    _exit(40 + child_index);
}

static void run_q2(void) {
    int pipefd[2];
    pid_t pids[CHILD_COUNT];
    Q2Instruction instr;

    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(1);
    }

    ignore_list(base_signals, BASE_SIGNAL_COUNT);

    for (int i = 0; i < CHILD_COUNT; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        if (pid == 0) {
            if (i == 0) {
                close(pipefd[1]);
                q2_sender_child(pipefd[0]);
            }
            close(pipefd[0]);
            close(pipefd[1]);
            q2_receiver_child(i);
        }
        pids[i] = pid;
    }

    close(pipefd[0]);
    install_handlers_for_list(base_signals, BASE_SIGNAL_COUNT);

    instr.target_child = pids[1];
    instr.parent_pid = getpid();
    instr.repeats = 2;
    if (write(pipefd[1], &instr, sizeof(instr)) != (ssize_t)sizeof(instr)) {
        perror("write q2 instruction");
    }
    close(pipefd[1]);

    printf("Q2 parent pid=%ld waiting while child pid=%ld signals child pid=%ld and the parent twice per signal.\n",
           (long)getpid(), (long)pids[0], (long)pids[1]);
    wait_for_children(CHILD_COUNT);
    print_pending_queue("Q2 parent pending after waits", base_signals, BASE_SIGNAL_COUNT);
    restore_defaults_for_list(base_signals, BASE_SIGNAL_COUNT);
}

static void send_signal_repeats(pid_t pid, const int *signals, int count, int repeats, const char *who) {
    printf("%s send each signal %d time(s) to pid=%ld\n", who, repeats, (long)pid);
    for (int i = 0; i < count; i++) {
        int signo = signals[i];
        for (int r = 0; r < repeats; r++) {
            if (kill(pid, signo) == -1) {
                perror("kill q3");
            }
        }
    }
}

static void q3_child_body(int child_index) {
    static const int triad[3] = {SIGINT, SIGQUIT, SIGTSTP};
    static const int rest[6] = {SIGABRT, SIGILL, SIGCHLD, SIGSEGV, SIGFPE, SIGHUP};
    const int *blocked = (child_index < 2) ? triad : rest;
    int blocked_count = (child_index < 2) ? 3 : 6;

    set_process_mask(blocked, blocked_count);

    printf("Q3 child index=%d pid=%ld inherited handlers after fork and now blocks %s set.\n",
           child_index, (long)getpid(), child_index < 2 ? "SIGINT/SIGQUIT/SIGTSTP" : "the remaining six signals");
    print_signal_list("Q3 child blocked:", blocked, blocked_count);

    sleep_all(g_fast ? 2U : 5U);
    print_pending_queue("Q3 child pending before sigwait calls", q3_signals, Q3_SIGNAL_COUNT);
    consume_pending_examples("Q3 child", blocked, blocked_count);
    print_pending_queue("Q3 child pending after sigwait calls", q3_signals, Q3_SIGNAL_COUNT);
    _exit(70 + child_index);
}

static void run_q3(void) {
    static const int triad[3] = {SIGINT, SIGQUIT, SIGTSTP};
    pid_t pids[CHILD_COUNT];

    printf("Q3 parent pid=%ld installs handlers, blocks SIGINT/SIGQUIT/SIGTSTP, then sends signals to itself before forking.\n",
           (long)getpid());
    install_handlers_for_list(q3_signals, Q3_SIGNAL_COUNT);
    set_process_mask(triad, 3);

    send_signal_repeats(getpid(), q3_signals, Q3_SIGNAL_COUNT, 3, "Q3 parent before fork");
    print_pending_queue("Q3 parent pending before fork", q3_signals, Q3_SIGNAL_COUNT);

    for (int i = 0; i < CHILD_COUNT; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        if (pid == 0) {
            q3_child_body(i);
        }
        pids[i] = pid;
    }

    sleep(1);
    for (int i = 0; i < CHILD_COUNT; i++) {
        char label[80];
        snprintf(label, sizeof(label), "Q3 parent to child[%d]", i);
        send_signal_repeats(pids[i], q3_signals, Q3_SIGNAL_COUNT, 3, label);
    }

    send_signal_repeats(getpid(), q3_signals, Q3_SIGNAL_COUNT, 3, "Q3 parent after fork to itself");
    print_pending_queue("Q3 parent pending after sends", q3_signals, Q3_SIGNAL_COUNT);
    consume_pending_examples("Q3 parent", triad, 3);
    print_pending_queue("Q3 parent pending after sigwait calls", q3_signals, Q3_SIGNAL_COUNT);

    wait_for_children(CHILD_COUNT);
    restore_defaults_for_list(q3_signals, Q3_SIGNAL_COUNT);
}

static void usage(const char *prog) {
    printf("Usage: %s <part1|q2|q3> [fast]\n", prog);
    printf("  part1 default uses 10 seconds per sum iteration as assigned; use fast for test output.\n");
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (argc >= 3 && strcmp(argv[2], "fast") == 0) {
        g_fast = 1;
    }

    if (strcmp(argv[1], "part1") == 0) {
        run_part1();
    } else if (strcmp(argv[1], "q2") == 0) {
        g_fast = 1;
        run_q2();
    } else if (strcmp(argv[1], "q3") == 0) {
        g_fast = 1;
        run_q3();
    } else {
        usage(argv[0]);
        return 1;
    }

    return 0;
}
