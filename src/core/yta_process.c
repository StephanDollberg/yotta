#include "yta_process.h"

#include <signal.h>

#include <sys/unistd.h>
#include <sys/wait.h>

#include <unistd.h>
#include <stdlib.h>

#include <stdio.h>
#include <string.h>

pid_t* worker_pids = NULL;
sig_atomic_t worker_count = 0;
char* pidfile_to_delete = NULL;

void forward_signal_to_workers(int signo) {
    if (worker_pids != NULL) {
        for (int i = 0; i < worker_count; ++i) {
            kill(worker_pids[i], signo);
        }
    }
}

void signal_handler(int signo) {
    forward_signal_to_workers(signo);
    if (worker_pids != NULL) {
        free(worker_pids);
    }

    remove(pidfile_to_delete);

    if (signo == SIGQUIT) {
        exit(0);
    } else {
        exit(1);
    }
}

void write_pidfile(char* pidfile_path) {
    FILE* pidfile = fopen(pidfile_path, "w");
    if (pidfile == NULL) {
        fprintf(stderr, "can't open pidfile");
        exit(1);
    }

    int pid = getpid();
    char pid_buf[10] = { 0 };
    sprintf(pid_buf, "%d", pid);

    size_t status = fwrite(pid_buf, strlen(pid_buf), 1, pidfile);
    if (status != 1) {
        fprintf(stderr, "can't write to pidfile");
        exit(1);
    };

    pidfile_to_delete = pidfile_path;

    fclose(pidfile);
}

int yta_fork_workers(int workers, char* pidfile_path) {
    write_pidfile(pidfile_path);

    worker_count = workers;
    worker_pids = (pid_t*)malloc(workers * sizeof(pid_t));

    if (worker_pids == NULL) {
        fprintf(stderr, "can't malloc worker worker_pids");
        exit(1);
    }

    pid_t worker_pid = 0;

    int worker_id = -1;

    // spawn initial worker set
    for (int i = 0; i < workers; i++) {
        worker_pid = fork();

        if (worker_pid == -1) {
            fprintf(stderr, "couldn't forky mcforkface");
            exit(1);
        }

        if (worker_pid == 0) {
            signal(SIGTERM, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            worker_id = i;
            break;
        } else {
            worker_pids[i] = worker_pid;
        }
    }


    // wait for children and restart workers
    while (worker_pid != 0) {
        int status = 0;
        int pid = waitpid(-1, &status, 0);

        fprintf(stderr, "worker died: %d\n", pid);

        for (int i = 0; i < workers; i++) {
            if (pid == worker_pids[i]) {
                worker_pid = fork();

                if (worker_pid == -1) {
                    fprintf(stderr, "couldn't reforky mcforkface");
                    exit(1);
                }

                if (worker_pid == 0) {
                    signal(SIGTERM, SIG_DFL);
                    signal(SIGQUIT, SIG_DFL);
                } else {
                    worker_pids[i] = worker_pid;
                }

                break;
            }
        }
    }

    return worker_id;
}
