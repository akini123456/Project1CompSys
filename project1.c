#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <time.h>
#include <string.h>
#include <signal.h>

#define HIDDEN_TOTAL 150
#define MAX_HITS_STORE 256
#define MAX_CHILDREN 5

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

static const char *TRACE_FILE = "trace.csv";
static const char *OUTPUT_FILE = "output.txt";

double now_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void append_trace(pid_t pid, double start_t, double end_t, int elems, long long bytes_sent) {
    FILE *fp = fopen(TRACE_FILE, "a");
    if (!fp) return;
    fprintf(fp, "%d,%.6f,%.6f,%d,%lld\n", pid, start_t, end_t, elems, bytes_sent);
    fclose(fp);
}

void explain_wait_status(pid_t pid, int status, FILE *out) {
    if (WIFEXITED(status)) {
        fprintf(out, "Child with PID=%ld terminated normally, exit status=%d\n",
                (long)pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        fprintf(out, "Child with PID=%ld was terminated by signal %d\n",
                (long)pid, WTERMSIG(status));
    } else if (WIFSTOPPED(status)) {
        fprintf(out, "Child with PID=%ld was stopped by signal %d\n",
                (long)pid, WSTOPSIG(status));
    }
}

void init_result(Result *r) {
    r->max = -2147483647;
    r->sum = 0;
    r->count = 0;
    r->hidden_found = 0;
    memset(r->hidden_pos, -1, sizeof(r->hidden_pos));
    r->slowest_pid = -1;
    r->slowest_time = 0.0;
    r->bytes_sent = 0;
}

Result compute_local(int *arr, int start, int end, FILE *out, int ret_code) {
    Result r;
    init_result(&r);

    pid_t pid = getpid();
    pid_t ppid = getppid();

    fprintf(out, "ECE 434 Sp26: I'm process %d with return arg %d and my parent is %d.\n",
            pid, ret_code, ppid);
    fflush(out);

    for (int i = start; i < end; i++) {
        if (arr[i] > r.max) r.max = arr[i];
        r.sum += arr[i];
        r.count++;

        if (arr[i] < 0 && r.hidden_found < MAX_HITS_STORE) {
            r.hidden_pos[r.hidden_found++] = i;
            fprintf(out,
                    "ECE 434 Sp26: I am process %d with return arg %d. I found the hidden key in position A[%d].\n",
                    pid, ret_code, i);
        }
    }

    fflush(out);
    return r;
}

void merge_result(Result *dst, Result *src) {
    if (src->max > dst->max) dst->max = src->max;
    dst->sum += src->sum;
    dst->count += src->count;

    for (int i = 0; i < src->hidden_found && dst->hidden_found < MAX_HITS_STORE; i++) {
        dst->hidden_pos[dst->hidden_found++] = src->hidden_pos[i];
    }

    dst->bytes_sent += src->bytes_sent;

    if (src->slowest_time > dst->slowest_time) {
        dst->slowest_time = src->slowest_time;
        dst->slowest_pid = src->slowest_pid;
    }
}

Result process_segment(int *arr, Task task, int *proc_counter, FILE *out) {
    double start_t = now_sec();
    Result total;
    init_result(&total);

    int seg_size = task.end - task.start;

    if (*proc_counter >= task.max_proc || seg_size <= 1000) {
        Result local = compute_local(arr, task.start, task.end, out, task.ret_code);
        double end_t = now_sec();
        local.slowest_pid = getpid();
        local.slowest_time = end_t - start_t;
        append_trace(getpid(), start_t, end_t, seg_size, 0);
        return local;
    }

    int children = task.branch;
    if (children > MAX_CHILDREN) children = MAX_CHILDREN;
    if (children > seg_size) children = seg_size;

    int pipes[MAX_CHILDREN][2];
    pid_t pids[MAX_CHILDREN];
    struct pollfd pfds[MAX_CHILDREN];
    int active = 0;

    int chunk = seg_size / children;
    int rem = seg_size % children;
    int cur = task.start;

    fprintf(out, "Process %d starting level %d, waiting for children...\n", getpid(), task.level);
    fflush(out);

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

            Task child_task;
            child_task.start = child_start;
            child_task.end = child_end;
            child_task.level = task.level + 1;
            child_task.ret_code = task.ret_code * 10 + (i + 1);
            child_task.max_proc = task.max_proc;
            child_task.branch = task.branch;

            int child_counter = *proc_counter + children;
            Result child_res = process_segment(arr, child_task, &child_counter, out);

            child_res.bytes_sent += sizeof(Result);

            if (write(pipes[i][1], &child_res, sizeof(Result)) == -1) {
                perror("write");
                close(pipes[i][1]);
                exit(1);
            }
            close(pipes[i][1]);

            sleep(1);
            exit((child_task.ret_code % 250) + 1);
        } else {
            pids[i] = pid;
            close(pipes[i][1]);
            pfds[i].fd = pipes[i][0];
            pfds[i].events = POLLIN;
            pfds[i].revents = 0;
            active++;
        }
    }

    while (active > 0) {
        int rc = poll(pfds, children, 5000);
        if (rc < 0) {
            perror("poll");
            break;
        }
        if (rc == 0) {
            fprintf(out, "Parent %d: poll timeout, continuing...\n", getpid());
            fflush(out);
            continue;
        }

        for (int i = 0; i < children; i++) {
            if (pfds[i].fd != -1 && (pfds[i].revents & POLLIN)) {
                Result child_res;
                ssize_t n = read(pfds[i].fd, &child_res, sizeof(Result));
                if (n > 0) {
                    merge_result(&total, &child_res);
                }
                close(pfds[i].fd);
                pfds[i].fd = -1;
                active--;
            }
        }
    }

    for (int i = 0; i < children; i++) {
        int status;
        pid_t w = waitpid(pids[i], &status, 0);
        explain_wait_status(w, status, out);
    }

    double end_t = now_sec();
    total.slowest_pid = (total.slowest_pid == -1) ? getpid() : total.slowest_pid;
    if (end_t - start_t > total.slowest_time) {
        total.slowest_time = end_t - start_t;
        total.slowest_pid = getpid();
    }

    append_trace(getpid(), start_t, end_t, seg_size, total.bytes_sent);
    fprintf(out, "Process %d finishing level %d.\n", getpid(), task.level);
    fflush(out);

    return total;
}

void generate_input_file(const char *filename, int L, int *arr) {
    srand((unsigned int)time(NULL));

    for (int i = 0; i < L; i++) {
        arr[i] = (rand() % 1000) + 1;
    }

    for (int i = 0; i < HIDDEN_TOTAL; i++) {
        int pos = rand() % L;
        arr[pos] = -((rand() % 100) + 1);
    }

    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("fopen input");
        exit(1);
    }
    for (int i = 0; i < L; i++) {
        fprintf(fp, "%d\n", arr[i]);
    }
    fclose(fp);
}

void init_output_files() {
    FILE *fp = fopen(OUTPUT_FILE, "w");
    if (fp) fclose(fp);

    fp = fopen(TRACE_FILE, "w");
    if (fp) {
        fprintf(fp, "pid,start_time,end_time,elements_processed,bytes_sent\n");
        fclose(fp);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <L> <H> <PN> [branch]\n", argv[0]);
        printf("Example: %s 12000 20 7 2\n", argv[0]);
        return 1;
    }

    int L = atoi(argv[1]);
    int H = atoi(argv[2]);
    int PN = atoi(argv[3]);
    int branch = (argc >= 5) ? atoi(argv[4]) : 2;

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

    int *arr = malloc(sizeof(int) * L);
    if (!arr) {
        perror("malloc");
        return 1;
    }

    init_output_files();
    generate_input_file("input.txt", L, arr);

    FILE *out = fopen(OUTPUT_FILE, "a");
    if (!out) {
        perror("fopen output");
        free(arr);
        return 1;
    }

    fprintf(out, "Starting Project 1 with L=%d, H=%d, PN=%d, branch=%d\n", L, H, PN, branch);
    fflush(out);

    if (system("pstree -p") == -1) {
        perror("system pstree");
    }

    Task root;
    root.start = 0;
    root.end = L;
    root.level = 0;
    root.ret_code = 1;
    root.max_proc = PN;
    root.branch = branch;

    int proc_counter = 1;
    double global_start = now_sec();
    Result final = process_segment(arr, root, &proc_counter, out);
    double global_end = now_sec();

    double avg = (final.count > 0) ? ((double)final.sum / final.count) : 0.0;

    fprintf(out, "\nFINAL RESULTS:\n");
    fprintf(out, "Max=%d, Avg=%.2f\n", final.max, avg);

    fprintf(out, "Printing up to H=%d hidden key locations requested:\n", H);
    int to_print = (H < final.hidden_found) ? H : final.hidden_found;
    for (int i = 0; i < to_print; i++) {
        fprintf(out, "Hidden key found at A[%d]\n", final.hidden_pos[i]);
    }

    fprintf(out, "\nROOT SUMMARY:\n");
    fprintf(out, "Total runtime = %.6f sec\n", global_end - global_start);
    fprintf(out, "Slowest child/process PID = %d\n", final.slowest_pid);
    fprintf(out, "Slowest observed time = %.6f sec\n", final.slowest_time);
    fprintf(out, "Total IPC volume = %lld bytes\n", final.bytes_sent);

    fclose(out);
    free(arr);

    return 0;
}
