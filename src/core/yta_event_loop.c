#include "yta_event_loop.h"
#include "yta_process.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/unistd.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>

#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

typedef unsigned char byte;

struct loop_core {
    int listen_fd;
    int epoll_fd;
};

static int create_and_bind(char* addr, char* port) {
    struct addrinfo hint;
    struct addrinfo* res;
    int ret;

    memset(&hint, 0, sizeof(hint));

    hint.ai_family = PF_UNSPEC;
    hint.ai_flags = AI_NUMERICHOST;

    ret = getaddrinfo(addr, port, &hint, &res);
    if (ret) {
        fprintf(stderr, "couldn't get addrinfo for addr and port");
        exit(1);
    }

    int listen_fd = 0;
    if ((listen_fd = socket(res->ai_family, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "failed creating socket\n");
        exit(1);
    }

    int enable = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) <
        0) {
        fprintf(stderr, "error setting SO_REUSEADDR");
        exit(1);
    }

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) <
        0) {
        fprintf(stderr, "error setting SO_REUSEPORT");
        exit(1);
    }

    if (bind(listen_fd, (struct sockaddr*)res->ai_addr, res->ai_addrlen) < 0) {
        fprintf(stderr, "can't bind the foo\n");
        exit(1);
    }

    free(res);

    return listen_fd;
}


static void make_socket_non_blocking(int listen_fd) {
    int flags = fcntl(listen_fd, F_GETFL, 0);
    if (flags == -1) {
        fprintf(stderr, "couldn't get socket opt list");
        exit(1);
    }

    flags |= O_NONBLOCK;
    int status = fcntl(listen_fd, F_SETFL, flags);
    if (status == -1) {
        fprintf(stderr, "couldn't set O_NONBLOCK on listening socket");
        exit(1);
    }
}


static void cleanup_context(struct yta_ctx* ctx) {
    close(ctx->fd);

    if (ctx->close_callback) {
        ctx->close_callback(ctx);
    }

    free(ctx);
}

static struct loop_core create_reactor(char* addr, char* port) {
    struct loop_core reactor;

    // both exit directly on error
    reactor.listen_fd = create_and_bind(addr, port);
    make_socket_non_blocking(reactor.listen_fd);

    int status = listen(reactor.listen_fd, SOMAXCONN);
    if (status == -1) {
        perror("error on listening");
        exit(1);
    }

    reactor.epoll_fd = epoll_create1(0);
    if (reactor.epoll_fd == -1) {
        perror("error creating epoll fd");
        exit(1);
    }

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    struct yta_ctx* ctx = (struct yta_ctx*)calloc(1, sizeof(struct yta_ctx));
    event.data.ptr = ctx;
    ctx->fd = reactor.listen_fd;
    status =
        epoll_ctl(reactor.epoll_fd, EPOLL_CTL_ADD, reactor.listen_fd, &event);
    if (status == -1) {
        perror("error adding listen_fd to epoll set");
        exit(1);
    }

    return reactor;
}

static inline void
deepreact_accept_loop(struct loop_core* reactor,
                      void (*accept_callback)(struct yta_ctx*),
                      pid_t worker_pid) {
    while (1) {
        struct sockaddr in_addr;
        socklen_t in_len;
        int infd;
        char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

        in_len = sizeof in_addr;
        infd = accept(reactor->listen_fd, &in_addr, &in_len);
        if (infd == -1) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                break;
            } else {
                perror("accept");
                break;
            }
        }

        int status = getnameinfo(&in_addr, in_len, hbuf, sizeof(hbuf), sbuf,
                                 sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
        if (status == 0) {
            fprintf(stderr, "Accepted connection on descriptor %d "
                            "(host=%s, port=%s, process=%d)\n",
                    infd, hbuf, sbuf, worker_pid);
        }

        make_socket_non_blocking(infd);

        int enable = 1;
        if (setsockopt(infd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) <
            0) {
            fprintf(stderr, "error setting TCP_NODELAY");
            exit(1);
        }

        struct yta_ctx* udata = (struct yta_ctx*)calloc(1, sizeof(struct yta_ctx));
        udata->fd = infd;

        struct epoll_event event;
        event.data.ptr = udata;
        event.events = EPOLLIN | EPOLLOUT | EPOLLET;
        status = epoll_ctl(reactor->epoll_fd, EPOLL_CTL_ADD, infd, &event);
        if (status == -1) {
            perror("epoll_ctl");
            exit(1);
        }

        accept_callback(udata);
    }
}

static inline int deepreact_read_loop(struct yta_ctx* udata) {
    while (udata->read_callback) {
        ssize_t count = read(udata->fd, udata->read_buf, udata->to_read);
        if (count == -1) {
            if (errno != EAGAIN) {
                perror("read");
                return 1;
            }

            break;
        } else if (count == 0) {
            return 1;
        }

        yta_callback read_callback = udata->read_callback;
        udata->read_callback = NULL;
        read_callback(udata, udata->read_buf, count);
    }

    return 0;
}

static inline int deepreact_write_handle_basic(struct yta_ctx* udata) {
    ssize_t count =
        write(udata->fd, (byte*)udata->wdata.basic_data.write_buf + udata->already_written,
              udata->to_write - udata->already_written);
    if (count == -1) {
        if (errno != EAGAIN) {
            perror("write");
            return 1;
        }

        return -1;
    } else if (count == 0) {
        assert(0);
    }
    udata->already_written += count;

    if (udata->already_written == udata->to_write) {
        yta_callback write_callback = udata->write_callback;
        udata->write_callback = NULL;
        write_callback(udata, udata->wdata.basic_data.write_buf,
                       udata->already_written);
    }

    return 0;
}

static inline int deepreact_write_handle_sendfile(struct yta_ctx* udata) {
    off_t tfile_counter = udata->already_written;
    ssize_t count =
        sendfile(udata->fd, udata->wdata.sendfile_data.file_fd, &tfile_counter,
                 udata->to_write - udata->already_written);
    if (count == -1) {
        if (errno != EAGAIN) {
            perror("write");
            return 1;
        }

        return -1;
    } else if (count == 0) {
        assert(0);
    }

    udata->already_written += count;
    if (udata->already_written == udata->to_write) {
        yta_callback write_callback = udata->write_callback;
        udata->write_callback = NULL;
        write_callback(udata, NULL, udata->already_written);
    }

    return 0;
}

static inline int deepreact_write_loop(struct yta_ctx* udata) {
    int done = 0;
    while (!done && udata->write_callback) {
        if (udata->wtype == BASIC_WTYPE) {
            done = deepreact_write_handle_basic(udata);
        } else {
            done = deepreact_write_handle_sendfile(udata);
        }
    }

    return done == -1 ? 0 : done;
}

static inline void deep_serve(char* addr, char* port,
                              void (*accept_callback)(struct yta_ctx* ctx)) {
    struct loop_core reac = create_reactor(addr, port);
    struct loop_core* reactor = &reac;

    const int MAX_EVENT_COUNT = 1024;
    struct epoll_event* events = (struct epoll_event*)calloc(
        MAX_EVENT_COUNT, sizeof(struct epoll_event));

    while (1) {
        int event_count =
            epoll_wait(reactor->epoll_fd, events, MAX_EVENT_COUNT, -1);

        // printf("%d ", event_count);

        for (int i = 0; i < event_count; i++) {
            struct yta_ctx* udata = (struct yta_ctx*)events[i].data.ptr;

            if (((events[i].events & EPOLLERR) ||
                 (events[i].events & EPOLLRDHUP)) &&
                (!(events[i].events & EPOLLIN ||
                   events[i].events & EPOLLOUT))) {

                if (events[i].events & EPOLLHUP) {
                    printf("Client disconnected\n");
                } else {
                    fprintf(stderr, "epoll error\n");
                    fprintf(stderr, "error: %s\n", strerror(errno));
                }

                cleanup_context(udata);
            } else if (reactor->listen_fd == udata->fd) {
                deepreact_accept_loop(reactor, accept_callback, getpid());
            } else {
                int done = 0;

                if (events[i].events & EPOLLIN) {
                    done = deepreact_read_loop(udata);
                }

                if (events[i].events & EPOLLOUT && !done) {
                    done = deepreact_write_loop(udata);
                }

                if (done || (!udata->write_callback && !udata->read_callback)) {
                    fprintf(stderr, "Closed connection on descriptor %d\n",
                            udata->fd);
                    cleanup_context(udata);
                }
            }
        }
    }

    free(events);

    close(reactor->listen_fd);
}


void yta_async_read(struct yta_ctx* ctx,
                        void (*callback)(struct yta_ctx* ctx, void* /* buf */,
                                         size_t /*count*/),
                        void* buf, size_t to_read) {
    ctx->read_callback = callback;
    ctx->read_buf = buf;
    ctx->already_read = 0;
    ctx->to_read = to_read;
}


void yta_async_write(struct yta_ctx* ctx,
                         void (*callback)(struct yta_ctx* ctx,
                                          void* /* buf */, size_t /*count*/),
                         void* buf, size_t to_write) {
    ctx->wtype = BASIC_WTYPE;
    ctx->write_callback = callback;
    ctx->wdata.basic_data.write_buf = buf;
    ctx->to_write = to_write;
    ctx->already_written = 0;
}


void yta_async_sendfile(struct yta_ctx* ctx,
                            void (*callback)(struct yta_ctx* ctx,
                                             void* /* buf */, size_t /*count*/),
                            int fd, size_t to_write, size_t offset) {
    ctx->wtype = SENDFILE_WTYPE;
    ctx->write_callback = callback;
    ctx->wdata.sendfile_data.file_fd = fd;
    // we add the offset to keep the logic simply when sending bytes
    ctx->to_write = to_write + offset;
    ctx->already_written = offset;
}

void yta_set_close_callback(struct yta_ctx* ctx, void (*callback)(struct yta_ctx* ctx)) {
    ctx->close_callback = callback;
}


void yta_close_context(struct yta_ctx* ctx) {
    ctx->write_callback = NULL;
    ctx->read_callback = NULL;
}


void yta_run(char* addr, char* port,
                void (*accept_callback)(struct yta_ctx* ctx)) {
//    int status = chroot("./"); 
//    if (status != 0) {
//	fprintf(stderr, "failed chrooting, please CAP_SYS_CHROOT");
//	exit(1);
//    }

    // TODO: replace with sigaction usage
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, sigterm_handler);
    yta_fork_workers();

    deep_serve(addr, port, accept_callback);
}
