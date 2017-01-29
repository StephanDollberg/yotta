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
char** stored_argv = NULL;
int* stored_listen_fds = NULL;
int upgraded = 0;

void forward_signal_to_workers(int signo) {
    if (worker_pids != NULL) {
        for (int i = 0; i < worker_count; ++i) {
            kill(worker_pids[i], signo);
        }
    }
}

char* create_listen_fds_env(int* listen_fds, int worker_count) {
    // max size per pid is 5, worker_count pids and worker_count - 1 spaces
    size_t buf_size = sizeof("listen_fds=") + worker_count * 5 + worker_count - 1;
    char* buf = calloc(1, buf_size);
    if (buf == NULL) {
        fprintf(stderr, "failed to allocated mem for listen fds, aborting ...");
        exit(1);
    }

    char* cur = buf;
    cur += sprintf(cur, "listen_fds=");
    for (int i = 0; i < worker_count - 1; i++) {
        int plus = sprintf(cur, "%d ", listen_fds[i]);
        cur = cur + plus;
    }
    cur += sprintf(cur, "%d", listen_fds[worker_count - 1]);

    return buf;
}

int* parse_listen_fds_env(char* listen_fds_env, int fd_count) {
    int* listen_fds = calloc(fd_count, sizeof(int));
    if (listen_fds == NULL) {
        fprintf(stderr, "failed to allocated mem for listen fds, aborting ...");
        exit(1);
    }
    char* iter = listen_fds_env;
    char* cur = listen_fds_env;

    int fd_iter = 0;
    while (*cur != '\0') {
        while (*cur != '\0' && *cur != ' ') {
            ++cur;
        }

        int fd = atoi(iter);

        listen_fds[fd_iter] = fd;

        ++fd_iter;

        if (*cur == '\0') {
            break;
        }

        ++cur;
        iter = cur;
    }

    return listen_fds;
}

void exit_and_cleanup_main(int signo) {
    if (worker_pids != NULL) {
        free(worker_pids);
    }

    remove(pidfile_to_delete);

    if (upgraded) {
        free(pidfile_to_delete);
    }

    if (signo == SIGQUIT) {
        exit(0);
    } else {
        exit(1);
    }
}

void signal_handler(int signo) {
    forward_signal_to_workers(signo);

    for (int i = 0; i < worker_count; ++i) {
        // we don't really care about return value here
        // workers have to die anyway
        waitpid(worker_pids[i], NULL, 0);
    }

    exit_and_cleanup_main(signo);
}

void upgrade_handler(int signo) {
    // TODO check for upgraded
    printf("upgrading handler triggered\n");
    (void)signo;

    const char* old_pid = ".old";
    size_t current_len = strlen(pidfile_to_delete);
    char* new_name = calloc(1, current_len + sizeof(old_pid) - 1);
    if (new_name == NULL) {
        fprintf(stderr, "failed to alloc mem on upgrade");
        return;
    }

    sprintf(new_name, "%s%s", pidfile_to_delete, old_pid);

    if (rename(pidfile_to_delete, new_name)) {
        fprintf(stderr, "failed to rename pid file, aborting upgrade");
        return;
    }
    pidfile_to_delete = new_name;

    char* buf = create_listen_fds_env(stored_listen_fds, worker_count);
    char* const envp[] = { buf, NULL };

    upgraded = 1;

    pid_t pid = fork();

    if (pid == -1) {
        perror("failed to fork for new binary");
        return;
    }

    if (pid == 0) {
        int status = execve(stored_argv[0], stored_argv, envp );

        if (status == -1) {
            perror("Failed to execve");
            exit(1);
        }
    }

    free(buf);
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

void clear_sigmask() {
    int status = 0;
    sigset_t signal_set;
    status = sigemptyset(&signal_set);

    if (status == -1) {
        perror("error initializing signal set");
        exit(1);
    }

    status = sigprocmask(SIG_SETMASK, &signal_set, NULL);
    if (status == -1) {
        perror("error setting sigprocmask");
        exit(1);
    }
}

int yta_fork_workers(int workers, char* pidfile_path, char** argv, int* listen_fds) {
    clear_sigmask();

    // TODO: replace with sigaction usage
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGUSR1, upgrade_handler);

    write_pidfile(pidfile_path);

    worker_count = workers;
    worker_pids = (pid_t*)calloc(workers, sizeof(pid_t));
    stored_argv = argv;
    stored_listen_fds = listen_fds;

    if (worker_pids == NULL) {
        fprintf(stderr, "can't calloc worker worker_pids");
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
            signal(SIGINT, SIG_DFL);
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

        if (pid == -1) {
            perror("waitpid failed while in worker respawner");
        }

        // TODO check for upgraded
        fprintf(stderr, "worker died: %d status: %d\n", pid, WIFEXITED(status));

        for (int i = 0; i < workers; i++) {
            if (pid == worker_pids[i] && !upgraded) {
                worker_pid = fork();

                if (worker_pid == -1) {
                    fprintf(stderr, "couldn't reforky mcforkface");
                    exit(1);
                }

                if (worker_pid == 0) {
                    signal(SIGTERM, SIG_DFL);
                    signal(SIGQUIT, SIG_DFL);
                    signal(SIGINT, SIG_DFL);
                    worker_id = i;
                } else {
                    worker_pids[i] = worker_pid;
                }

                break;
            }
        }
    }

    return worker_id;
}

void yta_daemonize() {
    pid_t pid = fork();

    switch (pid) {
        case 0: {
            break;
        }
        case -1: {
            fprintf(stderr, "failed daemonizing, exiting");
        }
        default: {
            exit(0);
        }
    }

    if (setsid() == -1) {
        perror("failed setsid in daemon, exiting daemon");
        exit(1);
    }
}
