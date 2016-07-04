#include "yta_process.h"

#include <signal.h>

#include <sys/unistd.h>
#include <sys/wait.h>

#include <unistd.h>
#include <stdlib.h>

#include <stdio.h>

pid_t* worker_pids = NULL;

void sigterm_handler(int signo) {
    (void)signo;

    if (worker_pids != NULL) {
        for (int i = 0; i < 4; ++i) {
            kill(worker_pids[i], SIGTERM);
        }
    }

    exit(1);
}

void yta_fork_workers() {
    int workers = 4;
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
            // prctl(PR_SET_NAME, (long)("nginy worker"));
            signal(SIGTERM, SIG_DFL);
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
                }

                worker_pids[i] = worker_pid;
                break;
            }
        }
    }
}
