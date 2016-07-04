#ifndef YOTTA_PROCESS
#define YOTTA_PROCESS

#ifdef __cplusplus
extern "C" {
#endif


void sigterm_handler(int /* signo */);

void yta_fork_workers();

#ifdef __cplusplus
}
#endif

#endif // YOTTA_PROCESS
