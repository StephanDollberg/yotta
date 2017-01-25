#ifndef YOTTA_PROCESS
#define YOTTA_PROCESS

#ifdef __cplusplus
extern "C" {
#endif


void signal_handler(int /* signo */);

int yta_fork_workers(int workers, char* pidfile_path, char** argv, int* listen_fds);

char* create_listen_fds_env(int* listen_fd, int worker_count);
int* parse_listen_fds_env(char* listen_fds_env, int fd_count);

void yta_daemonize();

#ifdef __cplusplus
}
#endif

#endif // YOTTA_PROCESS
