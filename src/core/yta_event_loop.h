#ifndef YTA_EVENT_LOOP
#define YTA_EVENT_LOOP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct yta_ctx;
struct loop_core;

typedef enum { YTA_OK, YTA_EXIT } yta_callback_status;
typedef yta_callback_status (*yta_io_callback)(struct yta_ctx*, void*, size_t);
typedef yta_callback_status (*yta_callback)(struct yta_ctx*);

enum yta_write_type { BASIC_WTYPE, SENDFILE_WTYPE };

struct yta_write_wtype {
    void* write_buf;
};

struct yta_sendfile_wtype {
    int file_fd;
};

union yta_write_data {
    struct yta_write_wtype basic_data;
    struct yta_sendfile_wtype sendfile_data;
};

struct yta_ctx {
    // public
    void* user_data;

    // private
    struct loop_core* reactor;
    int fd;

    yta_io_callback read_callback;
    void* read_buf;
    size_t already_read;
    size_t to_read;

    yta_io_callback write_callback;
    enum yta_write_type wtype;
    union yta_write_data wdata;
    size_t already_written;
    size_t to_write;

    yta_callback close_callback;

    int timer_fd;
    yta_callback timer_callback;
};

void yta_async_read(struct yta_ctx* ctx,
                        yta_io_callback callback,
                        void* buf, size_t to_read);


void yta_async_write(struct yta_ctx* ctx,
                         yta_io_callback callback,
                         void* buf, size_t to_write);


void yta_async_sendfile(struct yta_ctx* ctx,
                            yta_io_callback callback,
                            int fd, size_t to_write, size_t offset);

void yta_async_timer(struct yta_ctx* ctx, yta_callback callback, int timeout_seconds, int timeout_nanoseconds);


void yta_set_close_callback(struct yta_ctx* ctx, yta_callback callback);


void yta_run(char* addr, char* port, yta_callback callback);

#ifdef __cplusplus
}
#endif

#endif
