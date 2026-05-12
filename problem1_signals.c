#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define HIDDEN_TOTAL 150
#define MAX_HITS_STORE 256
#define MAX_CHILDREN 5
#define DEFAULT_L 12000
#define DEFAULT_H 20
#define DEFAULT_PN 4
#define DEFAULT_BRANCH 3

typedef enum {
    EXP_SIGINT_HANDLER = 1,
    EXP_SIGINT_IGNORED = 2
} ExperimentMode;

typedef struct {
    int max;
    long long sum;
    int count;
    int hidden_found;
    int hidden_pos[MAX_HITS_STORE];
    int slowest_pid;
    double slowest_time;
    long long bytes_sent;
} Result;

typedef struct {
    int start;
    int end;
    int level;
    int ret_code;
    int max_proc;
    int branch;
} Task;

typedef struct {
    pid_t pid;
    Result result;
    int ret_code;
    int hidden_count;
} ChildInfo;

static const char *g_trace_file = "outputs/problem1_trace.csv";
static ExperimentMode g_experiment = EXP_SIGINT_HANDLER;
static unsigned int g_rule1_sleep = 100;
static unsigned int g_rule3_parent_delay = 10;
static unsigned int g_rule3_child_sleep = 30;
static unsigned int g_rule3_quit_delay = 3;
static unsigned int g_orphan_sleep = 8;

static volatile sig_atomic_t g_secret_received = 0;
static volatile sig_atomic_t g_secret_signal = SIGTERM;
static volatile sig_atomic_t g_rule3_selected = 0;
static volatile sig_atomic_t g_sigint_seen = 0;
static volatile sig_atomic_t g_secret_handler_seen = 0;

static const char *signal_name(int signo) {
    switch (signo) {
        case SIGABRT: return "SIGABRT";
        case SIGCHLD: return "SIGCHLD";
        case SIGCONT: return "SIGCONT";
        case SIGFPE: return "SIGFPE";
        case SIGHUP: return "SIGHUP";
        case SIGILL: return "SIGILL";
        case SIGINT: return "SIGINT";
        case SIGQUIT: return "SIGQUIT";
        case SIGSEGV: return "SIGSEGV";
        case SIGSTOP: return "SIGSTOP";
        case SIGTERM: return "SIGTERM";
        case SIGTSTP: return "SIGTSTP";
        case SIGUSR1: return "SIGUSR1";
        case SIGUSR2: return "SIGUSR2";
        default: return "UNKNOWN";
    }
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void safe_sleep(const char *label, unsigned int seconds) {
    unsigned int left = seconds;

    while (left > 0) {
        left = sleep(left);
        if (left > 0) {
            printf("%s sleep interrupted, left=%u\n", label, left);
        }
    }
}

static void sigusr1_secret_handler(int signo, siginfo_t *info, void *context) {
    (void)context;
    g_secret_received = 1;
    g_secret_signal = (info != NULL) ? info->si_value.sival_int : SIGTERM;

    printf("handler sig=%d(%s) pid=%ld ppid=%ld sender=%ld secret=%d(%s)\n",
           signo, signal_name(signo), (long)getpid(), (long)getppid(),
           (info != NULL) ? (long)info->si_pid : -1L,
           (int)g_secret_signal, signal_name((int)g_secret_signal));
}

static void sigusr2_rule3_handler(int signo, siginfo_t *info, void *context) {
    (void)context;
    g_rule3_selected = 1;

    printf("handler sig=%d(%s) pid=%ld ppid=%ld sender=%ld rule=3\n",
           signo, signal_name(signo), (long)getpid(), (long)getppid(),
           (info != NULL) ? (long)info->si_pid : -1L);
}

static void sigint_print_handler(int signo, siginfo_t *info, void *context) {
    (void)context;
    g_sigint_seen++;

    printf("SIGINT handler sig=%d(%s) child=%ld ppid=%ld sender=%ld\n",
           signo, signal_name(signo), (long)getpid(), (long)getppid(),
           (info != NULL) ? (long)info->si_pid : -1L);
}

static void secret_termination_handler(int signo) {
    g_secret_handler_seen = 1;
    printf("secret handler pid=%ld sig=%d(%s)\n",
           (long)getpid(), signo, signal_name(signo));
}

static void install_handlers(void) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigusr1_secret_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigusr2_rule3_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, NULL);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = secret_termination_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    if (g_experiment == EXP_SIGINT_HANDLER) {
        sa.sa_sigaction = sigint_print_handler;
        sa.sa_flags = SA_SIGINFO;
    } else {
        sa.sa_handler = SIG_IGN;
    }
    sigaction(SIGINT, &sa, NULL);
}

static void reset_child_decision_flags(void) {
    g_secret_received = 0;
    g_secret_signal = SIGTERM;
    g_rule3_selected = 0;
    g_sigint_seen = 0;
    g_secret_handler_seen = 0;
}

static void append_trace(pid_t pid, double start_t, double end_t, int elems, long long bytes_sent) {
    FILE *fp = fopen(g_trace_file, "a");
    if (fp == NULL) {
        return;
    }
    fprintf(fp, "%ld,%.6f,%.6f,%d,%lld\n",
            (long)pid, start_t, end_t, elems, bytes_sent);
    fclose(fp);
}

static void print_status(pid_t pid, int status) {
    printf("[waitpid] pid=%ld raw_status=%d ", (long)pid, status);

    if (WIFEXITED(status)) {
        printf("WIFEXITED yes, WEXITSTATUS=%d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("WIFSIGNALED yes, WTERMSIG=%d (%s)\n",
               WTERMSIG(status), signal_name(WTERMSIG(status)));
    } else if (WIFSTOPPED(status)) {
        printf("WIFSTOPPED yes, WSTOPSIG=%d (%s)\n",
               WSTOPSIG(status), signal_name(WSTOPSIG(status)));
#ifdef WIFCONTINUED
    } else if (WIFCONTINUED(status)) {
        printf("WIFCONTINUED yes\n");
#endif
    } else {
        printf("no decoded status matched\n");
    }
}

static void init_result(Result *r) {
    r->max = -2147483647;
    r->sum = 0;
    r->count = 0;
    r->hidden_found = 0;
    memset(r->hidden_pos, -1, sizeof(r->hidden_pos));
    r->slowest_pid = -1;
    r->slowest_time = 0.0;
    r->bytes_sent = 0;
}

static void merge_stats(Result *dst, const Result *src) {
    if (src->max > dst->max) {
        dst->max = src->max;
    }
    dst->sum += src->sum;
    dst->count += src->count;
    dst->bytes_sent += src->bytes_sent;

    if (src->slowest_time > dst->slowest_time) {
        dst->slowest_time = src->slowest_time;
        dst->slowest_pid = src->slowest_pid;
    }
}

static void merge_hidden_nodes(Result *dst, const Result *src) {
    for (int i = 0; i < src->hidden_found && dst->hidden_found < MAX_HITS_STORE; i++) {
        dst->hidden_pos[dst->hidden_found++] = src->hidden_pos[i];
    }
}

static Result compute_local(const int *arr, int start, int end, int ret_code) {
    Result r;
    init_result(&r);

    printf("compute pid=%ld ppid=%ld arg=%d range=[%d,%d)\n",
           (long)getpid(), (long)getppid(), ret_code, start, end);

    for (int i = start; i < end; i++) {
        if (arr[i] > r.max) {
            r.max = arr[i];
        }
        r.sum += arr[i];
        r.count++;

        if (arr[i] < 0 && r.hidden_found < MAX_HITS_STORE) {
            r.hidden_pos[r.hidden_found++] = i;
        }
    }

    printf("metrics pid=%ld hidden=%d count=%d\n",
           (long)getpid(), r.hidden_found, r.count);

    return r;
}

static void send_secret_signal(pid_t pid, int secret_signal) {
    union sigval value;
    value.sival_int = secret_signal;

    printf("parent=%ld child=%ld send SIGUSR1 secret=%d(%s)\n",
           (long)getpid(), (long)pid, secret_signal, signal_name(secret_signal));

    if (sigqueue(pid, SIGUSR1, value) == -1) {
        perror("sigqueue SIGUSR1");
    }
}

static void send_rule3_signal(pid_t pid) {
    union sigval value;
    value.sival_int = 3;

    printf("parent=%ld child=%ld send SIGUSR2 rule=3\n",
           (long)getpid(), (long)pid);

    if (sigqueue(pid, SIGUSR2, value) == -1) {
        perror("sigqueue SIGUSR2");
    }
}

static void continue_child(pid_t pid, const char *reason) {
    struct timespec tiny;

    tiny.tv_sec = 0;
    tiny.tv_nsec = 50000000L;

    printf("%s parent=%ld child=%ld send SIGCONT\n",
           reason, (long)getpid(), (long)pid);
    if (kill(pid, SIGCONT) == -1) {
        perror("kill SIGCONT");
    }

    /* SIGSTOP fallback may need a second continue in redirected runs. */
    nanosleep(&tiny, NULL);
    if (kill(pid, SIGCONT) == -1) {
        perror("kill SIGCONT second");
    }
}

static void wait_for_one_child(pid_t pid) {
    int status;
    int done = 0;

    while (!done) {
        pid_t w = waitpid(pid, &status, WUNTRACED | WCONTINUED);
        if (w == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("waitpid");
            return;
        }

        print_status(w, status);
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            done = 1;
        }
    }
}

static void print_stop_if_available(pid_t pid) {
    int status;
    pid_t w = waitpid(pid, &status, WUNTRACED | WNOHANG);

    if (w == pid) {
        print_status(w, status);
    }
}

static void rule2_orphan_demo(void) {
    pid_t helper = fork();
    if (helper < 0) {
        perror("fork orphan demo");
        return;
    }

    if (helper == 0) {
        printf("orphan helper pid=%ld ppid=%ld start\n",
               (long)getpid(), (long)getppid());
        safe_sleep("orphan helper", g_orphan_sleep);
        printf("orphan helper pid=%ld ppid=%ld after parent exit\n",
               (long)getpid(), (long)getppid());
        _exit(0);
    }

    printf("rule=2 child=%ld helper=%ld orphan test\n",
           (long)getpid(), (long)helper);
}

static void wait_for_parent_decision_signal(void) {
    struct timespec tiny;

    tiny.tv_sec = 0;
    tiny.tv_nsec = 20000000L;

    /* Wait briefly for queued SIGUSR1/SIGUSR2 after SIGCONT. */
    for (int i = 0; i < 100 && !g_secret_received && !g_rule3_selected; i++) {
        nanosleep(&tiny, NULL);
    }
}

static void child_after_parent_decision(const Task *task, const Result *child_res) {
    int exit_arg = (task->ret_code % 250) + 1;

    printf("child=%ld arg=%d hidden=%d raise SIGTSTP\n",
           (long)getpid(), task->ret_code, child_res->hidden_found);
    raise(SIGTSTP);
    printf("child=%ld raise SIGSTOP fallback\n", (long)getpid());
    raise(SIGSTOP);

    wait_for_parent_decision_signal();
    printf("child=%ld continued secret=%d rule3=%d\n",
           (long)getpid(), (int)g_secret_received, (int)g_rule3_selected);

    if (g_secret_received) {
        printf("rule=2 child=%ld secret=%d(%s)\n",
               (long)getpid(), (int)g_secret_signal, signal_name((int)g_secret_signal));
        rule2_orphan_demo();
        safe_sleep("rule2 child", 1);
        printf("rule=2 child=%ld raise secret\n", (long)getpid());
        raise((int)g_secret_signal);

        if (g_secret_handler_seen) {
            struct sigaction sa;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = SIG_DFL;
            sigemptyset(&sa.sa_mask);
            sigaction((int)g_secret_signal, &sa, NULL);
            printf("rule=2 child=%ld default %s and raise again\n",
                   (long)getpid(), signal_name((int)g_secret_signal));
            raise((int)g_secret_signal);
        }

        _exit(128 + (int)g_secret_signal);
    }

    if (g_rule3_selected) {
        printf("rule=3 child=%ld sleep=%u\n", (long)getpid(), g_rule3_child_sleep);
        unsigned int left = sleep(g_rule3_child_sleep);
        printf("rule=3 child=%ld sleep_left=%u sigint_count=%d\n",
               (long)getpid(), left, (int)g_sigint_seen);
        printf("rule=3 child=%ld wait SIGQUIT\n", (long)getpid());
        for (;;) {
            pause();
        }
    }

    printf("rule=1 child=%ld sleep then exit(%d)\n",
           (long)getpid(), exit_arg);
    safe_sleep("rule1 child", g_rule1_sleep);
    printf("rule=1 child=%ld exit=%d\n",
           (long)getpid(), exit_arg);
    exit(exit_arg);
}

static void apply_decision_rules(ChildInfo children[], int n, Result *total) {
    int min_index = 0;
    int max_index = 0;

    for (int i = 1; i < n; i++) {
        if (children[i].hidden_count < children[min_index].hidden_count) {
            min_index = i;
        }
        if (children[i].hidden_count > children[max_index].hidden_count) {
            max_index = i;
        }
    }

    printf("parent=%ld siblings:", (long)getpid());
    for (int i = 0; i < n; i++) {
        printf(" child=%ld hidden=%d", (long)children[i].pid, children[i].hidden_count);
    }
    printf("\n");

    printf("parent=%ld merge lowest child=%ld hidden=%d\n",
           (long)getpid(), (long)children[min_index].pid, children[min_index].hidden_count);
    merge_hidden_nodes(total, &children[min_index].result);

    for (int i = 0; i < n; i++) {
        print_stop_if_available(children[i].pid);
    }

    for (int i = 0; i < n; i++) {
        pid_t child = children[i].pid;
        int hidden = children[i].hidden_count;

        if (i == max_index) {
            printf("rule=1 parent=%ld child=%ld hidden=%d\n",
                   (long)getpid(), (long)child, hidden);
            continue_child(child, "RULE 1");
        } else if (i == min_index) {
            printf("rule=3 parent=%ld child=%ld hidden=%d\n",
                   (long)getpid(), (long)child, hidden);
            send_rule3_signal(child);
            continue_child(child, "RULE 3");
            safe_sleep("rule3 parent", g_rule3_parent_delay);
            printf("rule=3 parent=%ld child=%ld send SIGINT\n",
                   (long)getpid(), (long)child);
            if (kill(child, SIGINT) == -1) {
                perror("kill SIGINT rule3");
            }
            safe_sleep("rule3 parent", g_rule3_quit_delay);
            printf("rule=3 parent=%ld child=%ld send SIGQUIT\n",
                   (long)getpid(), (long)child);
            if (kill(child, SIGQUIT) == -1) {
                perror("kill SIGQUIT rule3");
            }
        } else {
            printf("rule=2 parent=%ld child=%ld hidden=%d\n",
                   (long)getpid(), (long)child, hidden);
            send_secret_signal(child, SIGTERM);
            continue_child(child, "RULE 2");
        }
    }

    for (int i = 0; i < n; i++) {
        wait_for_one_child(children[i].pid);
    }
}

static Result process_segment(const int *arr, Task task, int *proc_counter) {
    double start_t = now_sec();
    Result total;
    init_result(&total);

    int seg_size = task.end - task.start;

    if (*proc_counter >= task.max_proc || seg_size <= 1000) {
        Result local = compute_local(arr, task.start, task.end, task.ret_code);
        double end_t = now_sec();
        local.slowest_pid = (int)getpid();
        local.slowest_time = end_t - start_t;
        append_trace(getpid(), start_t, end_t, seg_size, 0);
        return local;
    }

    int children = task.branch;
    if (children > MAX_CHILDREN) {
        children = MAX_CHILDREN;
    }
    if (children > seg_size) {
        children = seg_size;
    }

    int pipes[MAX_CHILDREN][2];
    pid_t pids[MAX_CHILDREN];
    struct pollfd pfds[MAX_CHILDREN];
    ChildInfo info[MAX_CHILDREN];
    int active = 0;

    int chunk = seg_size / children;
    int rem = seg_size % children;
    int cur = task.start;

    printf("proc=%ld level=%d children=%d\n",
           (long)getpid(), task.level, children);

    for (int i = 0; i < children; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            exit(1);
        }

        int child_start = cur;
        int child_len = chunk + (i < rem ? 1 : 0);
        int child_end = child_start + child_len;
        cur = child_end;

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }

        if (pid == 0) {
            close(pipes[i][0]);
            reset_child_decision_flags();
            install_handlers();

            Task child_task;
            child_task.start = child_start;
            child_task.end = child_end;
            child_task.level = task.level + 1;
            child_task.ret_code = task.ret_code * 10 + (i + 1);
            child_task.max_proc = task.max_proc;
            child_task.branch = task.branch;

            int child_counter = *proc_counter + children;
            Result child_res = process_segment(arr, child_task, &child_counter);
            child_res.bytes_sent += (long long)sizeof(Result);

            if (write(pipes[i][1], &child_res, sizeof(Result)) != (ssize_t)sizeof(Result)) {
                perror("write child result");
                _exit(1);
            }
            close(pipes[i][1]);

            child_after_parent_decision(&child_task, &child_res);
            _exit((child_task.ret_code % 250) + 1);
        }

        pids[i] = pid;
        info[i].pid = pid;
        info[i].ret_code = task.ret_code * 10 + (i + 1);
        info[i].hidden_count = 0;
        close(pipes[i][1]);
        pfds[i].fd = pipes[i][0];
        pfds[i].events = POLLIN;
        pfds[i].revents = 0;
        active++;
    }

    while (active > 0) {
        int rc = poll(pfds, children, 5000);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }
        if (rc == 0) {
            printf("parent=%ld poll timeout\n", (long)getpid());
            continue;
        }

        for (int i = 0; i < children; i++) {
            if (pfds[i].fd != -1 && (pfds[i].revents & POLLIN)) {
                Result child_res;
                ssize_t n = read(pfds[i].fd, &child_res, sizeof(Result));
                if (n == (ssize_t)sizeof(Result)) {
                    info[i].result = child_res;
                    info[i].hidden_count = child_res.hidden_found;
                    merge_stats(&total, &child_res);
                    printf("parent=%ld got child=%ld arg=%d hidden=%d\n",
                           (long)getpid(), (long)pids[i], info[i].ret_code,
                           child_res.hidden_found);
                } else {
                    perror("read child result");
                }
                close(pfds[i].fd);
                pfds[i].fd = -1;
                active--;
            }
        }
    }

    apply_decision_rules(info, children, &total);

    double end_t = now_sec();
    if (total.slowest_pid == -1) {
        total.slowest_pid = (int)getpid();
    }
    if (end_t - start_t > total.slowest_time) {
        total.slowest_time = end_t - start_t;
        total.slowest_pid = (int)getpid();
    }

    append_trace(getpid(), start_t, end_t, seg_size, total.bytes_sent);
    printf("proc=%ld done level=%d hidden=%d\n",
           (long)getpid(), task.level, total.hidden_found);

    return total;
}

static void generate_input_file(const char *filename, int L, int branch, int *arr) {
    int groups = branch * branch;
    int placed = 0;

    if (groups < 3) {
        groups = 3;
    }
    if (groups > 25) {
        groups = 25;
    }

    for (int i = 0; i < L; i++) {
        arr[i] = (i * 37) % 1000 + 1;
    }

    for (int g = 0; g < groups && placed < HIDDEN_TOTAL; g++) {
        int remaining_groups = groups - g;
        int remaining_hidden = HIDDEN_TOTAL - placed;
        int count = (2 * (g + 1));

        if (count > remaining_hidden - (remaining_groups - 1)) {
            count = remaining_hidden - (remaining_groups - 1);
        }
        if (g == groups - 1) {
            count = remaining_hidden;
        }

        int start = (int)((long long)g * L / groups);
        int end = (int)((long long)(g + 1) * L / groups);
        int width = end - start;
        if (width <= 0) {
            continue;
        }

        for (int k = 0; k < count && placed < HIDDEN_TOTAL; k++) {
            int pos = start + (int)(((long long)(k + 1) * width) / (count + 1));
            if (pos >= L) {
                pos = L - 1;
            }
            arr[pos] = -((placed % 100) + 1);
            placed++;
        }
    }

    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        perror("fopen input");
        exit(1);
    }
    for (int i = 0; i < L; i++) {
        fprintf(fp, "%d\n", arr[i]);
    }
    fclose(fp);
}

static void init_trace_file(void) {
    mkdir("outputs", 0775);

    FILE *fp = fopen(g_trace_file, "w");
    if (fp != NULL) {
        fprintf(fp, "pid,start_time,end_time,elements_processed,bytes_sent\n");
        fclose(fp);
    }
}

static void usage(const char *prog) {
    printf("Usage: %s <exp1|exp2> [fast] [L H PN branch]\n", prog);
    printf("Defaults: L=%d H=%d PN=%d branch=%d. Add 'fast' to shorten sleeps for test runs.\n",
           DEFAULT_L, DEFAULT_H, DEFAULT_PN, DEFAULT_BRANCH);
}

int main(int argc, char *argv[]) {
    int L = DEFAULT_L;
    int H = DEFAULT_H;
    int PN = DEFAULT_PN;
    int branch = DEFAULT_BRANCH;
    int numeric_index = 2;
    int fast = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "exp1") == 0) {
        g_experiment = EXP_SIGINT_HANDLER;
    } else if (strcmp(argv[1], "exp2") == 0) {
        g_experiment = EXP_SIGINT_IGNORED;
    } else {
        usage(argv[0]);
        return 1;
    }

    if (argc >= 3 && strcmp(argv[2], "fast") == 0) {
        fast = 1;
        numeric_index = 3;
        g_rule1_sleep = 5;
        g_rule3_parent_delay = 2;
        g_rule3_child_sleep = 6;
        g_rule3_quit_delay = 2;
        g_orphan_sleep = 3;
    }

    if (argc - numeric_index >= 4) {
        L = atoi(argv[numeric_index]);
        H = atoi(argv[numeric_index + 1]);
        PN = atoi(argv[numeric_index + 2]);
        branch = atoi(argv[numeric_index + 3]);
    }

    if (L < 12000) {
        printf("L must be >= 12000\n");
        return 1;
    }
    if (PN <= 0 || PN > 50) {
        printf("PN must be between 1 and 50\n");
        return 1;
    }
    if (!(branch == 2 || branch == 3 || branch == 5)) {
        printf("branch must be 2, 3, or 5\n");
        return 1;
    }

    int *arr = malloc(sizeof(int) * (size_t)L);
    if (arr == NULL) {
        perror("malloc");
        return 1;
    }

    init_trace_file();
    generate_input_file("input.txt", L, branch, arr);
    install_handlers();

    printf("Problem1 %s L=%d H=%d PN=%d branch=%d fast=%d\n",
           g_experiment == EXP_SIGINT_HANDLER ? "Experiment 1: SIGINT handler" : "Experiment 2: SIGINT ignored",
           L, H, PN, branch, fast);
    printf("root pid=%ld, pstree -p %ld\n",
           (long)getpid(), (long)getpid());

    Task root;
    root.start = 0;
    root.end = L;
    root.level = 0;
    root.ret_code = 1;
    root.max_proc = PN;
    root.branch = branch;

    int proc_counter = 1;
    double global_start = now_sec();
    Result final = process_segment(arr, root, &proc_counter);
    double global_end = now_sec();

    double avg = (final.count > 0) ? ((double)final.sum / final.count) : 0.0;

    printf("\nFINAL RESULTS:\n");
    printf("Max=%d, Avg=%.2f\n", final.max, avg);
    printf("Propagated hidden count=%d\n", final.hidden_found);
    printf("First %d propagated hidden locations:\n", H);
    int to_print = (H < final.hidden_found) ? H : final.hidden_found;
    for (int i = 0; i < to_print; i++) {
        printf("A[%d]\n", final.hidden_pos[i]);
    }

    printf("\nROOT SUMMARY:\n");
    printf("Total runtime = %.6f sec\n", global_end - global_start);
    printf("Slowest child/process PID = %d\n", final.slowest_pid);
    printf("Slowest observed time = %.6f sec\n", final.slowest_time);
    printf("Total IPC volume = %lld bytes\n", final.bytes_sent);

    free(arr);
    return 0;
}
