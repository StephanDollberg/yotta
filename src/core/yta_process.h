#ifndef YOTTA_PROCESS
#define YOTTA_PROCESS

#ifdef __cplusplus
extern "C" {
#endif


void signal_handler(int /* signo */);

int yta_fork_workers(int workers, char* pidfile_path);

#ifdef __cplusplus
}
#endif

#endif // YOTTA_PROCESS
