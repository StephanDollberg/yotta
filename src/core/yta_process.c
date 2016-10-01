#include "yta_process.h"

#include <signal.h>

#include <sys/unistd.h>
#include <sys/wait.h>

#include <unistd.h>
#include <stdlib.h>

#include <stdio.h>

pid_t* worker_pids = NULL;
sig_atomic_t worker_count = 0;

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
    exit(1);
}

void yta_fork_workers(int workers) {
    worker_count = workers;
    worker_pids = (pid_t*)malloc(workers * sizeof(pid_t));

    if (worker_pids == NULL) {
        fprintf(stderr, "can't malloc worker worker_pids");
        exit(1);
    }

    pid_t worker_pid = 0;

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
                }

                worker_pids[i] = worker_pid;
                break;
            }
        }
    }
}
