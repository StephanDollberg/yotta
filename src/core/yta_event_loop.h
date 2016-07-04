#ifndef YTA_EVENT_LOOP
#define YTA_EVENT_LOOP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct yta_ctx;

typedef void (*yta_callback)(struct yta_ctx*, void*, size_t);

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
    int fd;

    yta_callback read_callback;
    void* read_buf;
    size_t already_read;
    size_t to_read;

    yta_callback write_callback;
    enum yta_write_type wtype;
    union yta_write_data wdata;
    size_t already_written;
    size_t to_write;

    void (*close_callback)(struct yta_ctx*);
};

void yta_async_read(struct yta_ctx* ctx,
                        void (*callback)(struct yta_ctx* ctx, void* /* buf */,
                                         size_t /*count*/),
                        void* buf, size_t to_read);


void yta_async_write(struct yta_ctx* ctx,
                         void (*callback)(struct yta_ctx* ctx,
                                          void* /* buf */, size_t /*count*/),
                         void* buf, size_t to_write);


void yta_async_sendfile(struct yta_ctx* ctx,
                            void (*callback)(struct yta_ctx* ctx,
                                             void* /* buf */, size_t /*count*/),
                            int fd, size_t to_write, size_t offset);


void yta_set_close_callback(struct yta_ctx* ctx, void (*callback)(struct yta_ctx* ctx));


void yta_close_context(struct yta_ctx* ctx);


void yta_run(char* addr, char* port,
                void (*accept_callback)(struct yta_ctx* ctx));

#ifdef __cplusplus
}
#endif

#endif
