#include "yta_event_loop.h"
#include "yta_process.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/unistd.h>
#include <sys/signalfd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

typedef unsigned char byte;

struct loop_core {
    int listen_fd;
    struct yta_ctx* listen_fd_ctx; // only stored to free later

    int socket_epoll_fd;
    int timer_epoll_fd;
    int signal_fd;

    int master_epoll_fd;

    int current_clients;
};

enum yta_loop_status { YTA_LOOP_CONTINUE, YTA_LOOP_AGAIN , YTA_LOOP_ERROR };

// some of util functions based on
// https://banu.com/blog/2/how-to-use-epoll-a-complete-example-in-c/

static int make_listen_socket(char* addr, char* port) {
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
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        fprintf(stderr, "error setting SO_REUSEADDR");
        exit(1);
    }

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) < 0) {
        fprintf(stderr, "error setting SO_REUSEPORT");
        exit(1);
    }

    if (bind(listen_fd, (struct sockaddr*)res->ai_addr, res->ai_addrlen) < 0) {
        fprintf(stderr, "can't bind the foo\n");
        exit(1);
    }

    do {
        struct addrinfo* next = res->ai_next;
        free(res);
        res = next;
    } while(res);

    return listen_fd;
}

static void set_nonblock(int listen_fd) {
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

static void cleanup_context(struct loop_core* reactor, struct yta_ctx* ctx) {
    close(ctx->fd);

    if (ctx->timer_fd != 0) {
        close(ctx->timer_fd);
    }

    if (ctx->close_callback) {
        ctx->close_callback(ctx);
    }

    free(ctx);
    --reactor->current_clients;
}

static struct loop_core create_reactor(int listen_fd) {
    struct loop_core reactor;
    reactor.current_clients = 0;

    reactor.listen_fd = listen_fd;

    reactor.socket_epoll_fd = epoll_create1(0);
    if (reactor.socket_epoll_fd == -1) {
        perror("error creating epoll fd");
        exit(1);
    }

    struct epoll_event listen_event;
    listen_event.events = EPOLLIN | EPOLLET;
    reactor.listen_fd_ctx = (struct yta_ctx*)calloc(1, sizeof(struct yta_ctx));
    if (reactor.listen_fd_ctx == NULL) {
        fprintf(stderr, "failed to allocate mem for listenfd ctx, aborting");
        exit(1);
    }
    listen_event.data.ptr = reactor.listen_fd_ctx;
    reactor.listen_fd_ctx->fd = reactor.listen_fd;
    int status = epoll_ctl(reactor.socket_epoll_fd, EPOLL_CTL_ADD, reactor.listen_fd,
                       &listen_event);
    if (status == -1) {
        perror("error adding listen_fd to epoll set");
        exit(1);
    }

    reactor.timer_epoll_fd = epoll_create1(0);
    if (reactor.timer_epoll_fd == -1) {
        perror("error creating timer epoll fd");
        exit(1);
    }

    sigset_t signal_set;
    status = sigemptyset(&signal_set);
    if (status == -1) {
        perror("error initializing signal set");
        exit(1);
    }

    status = sigaddset(&signal_set, SIGQUIT);
    if (status == -1) {
        perror("error adding SIGQUIT to sig mask");
        exit(1);
    }

    status = sigprocmask(SIG_SETMASK, &signal_set, NULL);
    if (status == -1) {
        perror("error setting sigprocmask");
        exit(1);
    }

    reactor.signal_fd = signalfd(-1, &signal_set, SFD_NONBLOCK);
    if (reactor.signal_fd == -1) {
        perror("error creating signal fd");
        exit(1);
    }

    reactor.master_epoll_fd = epoll_create1(0);
    if (reactor.master_epoll_fd == -1) {
        perror("error creating master epoll fd");
        exit(1);
    }

    struct epoll_event socket_event;
    socket_event.data.fd = reactor.socket_epoll_fd;
    socket_event.events = EPOLLIN | EPOLLET;
    status = epoll_ctl(reactor.master_epoll_fd, EPOLL_CTL_ADD, reactor.socket_epoll_fd,
                       &socket_event);
    if (status == -1) {
        perror("error adding socket epoll fd to epoll set");
        exit(1);
    }

    struct epoll_event timer_event;
    timer_event.data.fd = reactor.timer_epoll_fd;
    timer_event.events = EPOLLIN | EPOLLET;
    status = epoll_ctl(reactor.master_epoll_fd, EPOLL_CTL_ADD, reactor.timer_epoll_fd,
                       &timer_event);
    if (status == -1) {
        perror("error adding timer epoll fd to epoll set");
        exit(1);
    }

    struct epoll_event signal_event;
    signal_event.data.fd = reactor.signal_fd;
    signal_event.events = EPOLLIN | EPOLLET;
    status = epoll_ctl(reactor.master_epoll_fd, EPOLL_CTL_ADD, reactor.signal_fd, &signal_event);
    if (status == -1) {
        perror("error adding signal fd to epoll set");
        exit(1);
    }

    return reactor;
}

static void accept_loop(struct loop_core* reactor, yta_callback accept_callback,
                               pid_t worker_pid) {
    while (1) {
        struct sockaddr in_addr;
        socklen_t in_len;
        int infd;
        char host_buf[NI_MAXHOST], port_buf[NI_MAXSERV];

        in_len = sizeof(in_addr);
        infd = accept(reactor->listen_fd, &in_addr, &in_len);
        if (infd == -1) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                break;
            } else {
                perror("accept");
                break;
            }
        }

        int status = getnameinfo(&in_addr, in_len, host_buf, sizeof(host_buf), port_buf,
                                 sizeof(port_buf), NI_NUMERICHOST | NI_NUMERICSERV);
        if (status == 0) {
            fprintf(stderr, "Accepted connection on descriptor %d "
                            "(host=%s, port=%s, process=%d)\n",
                    infd, host_buf, port_buf, worker_pid);
        }

        set_nonblock(infd);

        int enable = 1;
        if (setsockopt(infd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) < 0) {
            fprintf(stderr, "error setting TCP_NODELAY");
            exit(1);
        }

        struct yta_ctx* udata = (struct yta_ctx*)calloc(1, sizeof(struct yta_ctx));
        if (udata == NULL) {
            fprintf(stderr, "failed to allocate mem for context, continueing");
            close(infd);
            continue;
        }
        udata->fd = infd;
        udata->reactor = reactor;

        yta_callback_status callback_status = accept_callback(udata);
        if (callback_status == YTA_EXIT) {
            free(udata);
            continue;
        }

        struct epoll_event event;
        event.data.ptr = udata;
        event.events = EPOLLIN | EPOLLOUT | EPOLLET;
        status = epoll_ctl(reactor->socket_epoll_fd, EPOLL_CTL_ADD, infd, &event);
        if (status == -1) {
            fprintf(stderr, "error adding accepted socket to epoll loop");
            exit(1);
        }

        ++reactor->current_clients;
    }
}

static int read_loop(struct yta_ctx* udata) {
    while (udata->read_callback) {
        ssize_t count = read(udata->fd, udata->read_buf, udata->to_read);
        if (count == -1) {
            if (errno != EAGAIN) {
                perror("read");
                return YTA_LOOP_ERROR;
            }

            break;
        } else if (count == 0) {
            return YTA_LOOP_ERROR;
        }

        yta_io_callback read_callback = udata->read_callback;
        udata->read_callback = NULL;
        if (read_callback(udata, udata->read_buf, count) != YTA_OK) {
            return YTA_LOOP_ERROR;
        }
    }

    return YTA_LOOP_CONTINUE;
}

static int write_handle_basic(struct yta_ctx* udata) {
    ssize_t count = write(udata->fd, (byte*)udata->wdata.basic_data.write_buf +
                                         udata->already_written,
                          udata->to_write - udata->already_written);
    if (count == -1) {
        if (errno != EAGAIN) {
            perror("write basic");
            return YTA_LOOP_ERROR;
        }

        return YTA_LOOP_AGAIN;
    } else if (count == 0) {
        assert(0);
    }
    udata->already_written += count;

    if (udata->already_written == udata->to_write) {
        yta_io_callback write_callback = udata->write_callback;
        udata->write_callback = NULL;
        if (write_callback(udata, udata->wdata.basic_data.write_buf,
                           udata->already_written) != YTA_OK) {
            return YTA_LOOP_ERROR;
        }
    }

    return YTA_LOOP_CONTINUE;
}

static int write_handle_sendfile(struct yta_ctx* udata) {
    off_t tfile_counter = udata->already_written;
    ssize_t count = sendfile(udata->fd, udata->wdata.sendfile_data.file_fd,
                             &tfile_counter, udata->to_write - udata->already_written);
    if (count == -1) {
        if (errno != EAGAIN) {
            perror("write sendfile");
            return YTA_LOOP_ERROR;
        }

        return YTA_LOOP_AGAIN;
    } else if (count == 0) {
        assert(0);
    }

    udata->already_written += count;
    if (udata->already_written == udata->to_write) {
        yta_io_callback write_callback = udata->write_callback;
        udata->write_callback = NULL;
        if (write_callback(udata, NULL, udata->already_written) != YTA_OK) {
            return YTA_LOOP_ERROR;
        }
    }

    return YTA_LOOP_CONTINUE;
}

static int write_loop(struct yta_ctx* udata) {
    enum yta_loop_status status = YTA_LOOP_CONTINUE;
    while (status == YTA_LOOP_CONTINUE && udata->write_callback) {
        if (udata->wtype == BASIC_WTYPE) {
            status = write_handle_basic(udata);
        } else {
            status = write_handle_sendfile(udata);
        }
    }

    return status;
}

static void serve_timers(struct loop_core* reactor, struct epoll_event* events,
                                const int MAX_EVENT_COUNT) {
    int event_count = epoll_wait(reactor->timer_epoll_fd, events, MAX_EVENT_COUNT, -1);

    for (int i = 0; i < event_count; i++) {
        struct yta_ctx* ctx = (struct yta_ctx*)events[i].data.ptr;

        if (((events[i].events & EPOLLERR) || (events[i].events & EPOLLRDHUP)) &&
            (!(events[i].events & EPOLLIN || events[i].events & EPOLLOUT))) {

            if (events[i].events & EPOLLHUP) {
                printf("Client disconnected\n");
            } else {
                fprintf(stderr, "epoll error\n");
                fprintf(stderr, "error: %s\n", strerror(errno));
            }

            cleanup_context(reactor, ctx);
        } else {
            if (events[i].events & EPOLLIN) {
                if (ctx->timer_callback(ctx) != YTA_OK) {
                    cleanup_context(reactor, ctx);
                }
            } else {
                assert(0);
            }
        }
    }
}

static void serve_sockets(struct loop_core* reactor, struct epoll_event* events,
                                 const int MAX_EVENT_COUNT,
                                 yta_callback accept_callback) {
    int event_count = epoll_wait(reactor->socket_epoll_fd, events, MAX_EVENT_COUNT, -1);

    for (int i = 0; i < event_count; i++) {
        struct yta_ctx* udata = (struct yta_ctx*)events[i].data.ptr;

        if (((events[i].events & EPOLLERR) || (events[i].events & EPOLLRDHUP)) &&
            (!(events[i].events & EPOLLIN || events[i].events & EPOLLOUT))) {

            if (events[i].events & EPOLLHUP) {
                printf("Client disconnected\n");
            } else {
                fprintf(stderr, "epoll error\n");
                fprintf(stderr, "error: %s\n", strerror(errno));
            }

            cleanup_context(reactor, udata);
        } else if (reactor->listen_fd == udata->fd) {
            accept_loop(reactor, accept_callback, getpid());
        } else {
            enum yta_loop_status status = 0;

            if (events[i].events & EPOLLIN) {
                status = read_loop(udata);
            }

            if (events[i].events & EPOLLOUT && status == YTA_LOOP_CONTINUE) {
                status = write_loop(udata);
            }

            if (status == YTA_LOOP_ERROR) {
                fprintf(stderr, "Closed connection on descriptor %d\n", udata->fd);
                cleanup_context(reactor, udata);
            }
        }
    }
}

static void serve(int listen_fd, yta_callback accept_callback) {
    struct loop_core reactor = create_reactor(listen_fd);

    const int MAX_EVENT_COUNT = 1024;
    struct epoll_event events[MAX_EVENT_COUNT];

    struct epoll_event master_event;

    int terminated = 0;

    while (!terminated || reactor.current_clients) {
        // we only extract a single event as the socket and timer events can
        // influence
        // each other (remove fds from the other epoll set)
        // and such make the inner epoll_wait block
        epoll_wait(reactor.master_epoll_fd, &master_event, 1, -1);

        if (master_event.data.fd == reactor.socket_epoll_fd) {
            serve_sockets(&reactor, events, MAX_EVENT_COUNT, accept_callback);
        } else if (master_event.data.fd == reactor.timer_epoll_fd) {
            serve_timers(&reactor, events, MAX_EVENT_COUNT);
        } else if (master_event.data.fd == reactor.signal_fd) {
            printf("Worker ordered to terminate");
            terminated = 1;
            free(reactor.listen_fd_ctx);
        }
    }

    close(reactor.socket_epoll_fd);
    close(reactor.timer_epoll_fd);
    close(reactor.signal_fd);
    close(reactor.master_epoll_fd);

    printf("Worker terminated\n");
}

void yta_async_read(struct yta_ctx* ctx, yta_io_callback callback, void* buf,
                    size_t to_read) {
    ctx->read_callback = callback;
    ctx->read_buf = buf;
    ctx->already_read = 0;
    ctx->to_read = to_read;
}

void yta_async_write(struct yta_ctx* ctx, yta_io_callback callback, void* buf,
                     size_t to_write) {
    ctx->wtype = BASIC_WTYPE;
    ctx->write_callback = callback;
    ctx->wdata.basic_data.write_buf = buf;
    ctx->to_write = to_write;
    ctx->already_written = 0;
}

void yta_async_sendfile(struct yta_ctx* ctx, yta_io_callback callback, int fd,
                        size_t to_write, size_t offset) {
    ctx->wtype = SENDFILE_WTYPE;
    ctx->write_callback = callback;
    ctx->wdata.sendfile_data.file_fd = fd;
    // we add the offset to keep the logic simply when sending bytes
    ctx->to_write = to_write + offset;
    ctx->already_written = offset;
}

void yta_async_timer(struct yta_ctx* ctx, yta_callback callback, int timeout_seconds,
                     int timeout_nanoseconds) {
    ctx->timer_callback = callback;

    if (ctx->timer_fd == 0) {
        ctx->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (ctx->timer_fd == -1) {
            printf("can't create timerfd\n");
        }

        struct epoll_event event;
        event.data.ptr = ctx;
        event.events = EPOLLIN | EPOLLET;
        int status =
            epoll_ctl(ctx->reactor->timer_epoll_fd, EPOLL_CTL_ADD, ctx->timer_fd, &event);
        if (status == -1) {
            perror("epoll_ctl");
            exit(1);
        }
    }

    struct timespec interval = {.tv_sec = 0, .tv_nsec = 0 };
    struct timespec value = {.tv_sec = timeout_seconds, .tv_nsec = timeout_nanoseconds };
    struct itimerspec timeout_spec = {.it_interval = interval, .it_value = value };

    int status = timerfd_settime(ctx->timer_fd, 0, &timeout_spec, NULL);
    if (status != 0) {
        printf("can't settime timerfd\n");
    }
}

void yta_set_close_callback(struct yta_ctx* ctx, yta_callback callback) {
    ctx->close_callback = callback;
}

int* get_listen_fds(int worker_count, char* addr, char* port) {
    char* listen_fds_env = getenv("listen_fds");

    if (listen_fds_env == NULL) {
        printf("creating new fd set\n");
        int* listen_fds = calloc(worker_count, sizeof(int));
        if (listen_fds == NULL) {
            fprintf(stderr, "failed to allocated mem for listen fds, aborting ...");
            exit(1);
        }

        for (int i = 0; i < worker_count; ++i) {
            listen_fds[i] = make_listen_socket(addr, port);
            set_nonblock(listen_fds[i]);

            int status = listen(listen_fds[i], SOMAXCONN);
            if (status == -1) {
                perror("error on listening");
                exit(1);
            }
        }

        return listen_fds;
    } else {
        printf("parsing existing fd set\n");
        return parse_listen_fds_env(listen_fds_env, worker_count);
    }
}

void yta_run(char** argv, char* addr, char* port, char* pidfile_path, int daemonize, yta_callback accept_callback) {
    if (daemonize) {
        yta_daemonize();
    }

    const int worker_count = 4;

    int* listen_fds = get_listen_fds(worker_count, addr, port);

    int worker_id = yta_fork_workers(worker_count, pidfile_path, argv, listen_fds);

    serve(listen_fds[worker_id], accept_callback);

    free(listen_fds);
}
