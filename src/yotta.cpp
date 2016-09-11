#include <algorithm>
#include <experimental/string_view>
#include <unordered_map>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "core/yta_event_loop.h"
#include "http/yta_http.hpp"

#include "picohttpparser/picohttpparser.h"

const std::size_t MAX_HEADERS = 20;
const std::size_t MAX_BUFFER_SIZE = 1024;

typedef unsigned char byte;

struct pico_parser {
    const char* method = nullptr;
    std::size_t method_len = 0;
    const char* path = nullptr;
    std::size_t path_len = 0;
    int pret = 0;
    int minor_version = 0;
    struct phr_header headers[MAX_HEADERS];
    std::size_t num_headers = MAX_HEADERS;
};

struct user_data {
    std::size_t counter;

    char buf[MAX_BUFFER_SIZE];
    char response_buf[MAX_BUFFER_SIZE];

    bool finalized;

    int response_size;
    bool content;

    int file_fd;
    int file_size;
    int offset = 0;

    pico_parser parser;

    struct stat file_stat;
};

void return_400(user_data* udata) {
    auto ret = yta::http::serve_400(udata->response_buf);
    udata->response_size = ret - udata->response_buf;
    udata->content = false;
    udata->finalized = true;
}

void return_404(user_data* udata) {
    auto ret = yta::http::serve_404(udata->response_buf);
    udata->response_size = ret - udata->response_buf;
    udata->content = false;
    udata->finalized = true;
}

int parse_url(yta_ctx* ctx, const char* at, size_t length) {
    user_data* udata = static_cast<user_data*>(ctx->user_data);

    // parse query string
    auto pos = std::find(at, at + length, '?');
    if (pos != at + length) {
        length = pos - at;
    }

    // clean path
    char* path = const_cast<char*>(at); // UB
    auto new_length = yta::http::clean_path(path, length);
    *(--path) = '.';
    ++new_length;

    // append index.html to ./
    if (path[new_length - 1] == '/') {
        const char index_html[] = "index.html";
        std::copy(index_html, index_html + 10, path + new_length);
        new_length += 10;
    }

    path[new_length] = '\0';

    int ffd = open(path, O_RDONLY);

    if (ffd == -1) {
        return_404(udata);
        return 0;
    }

    struct stat file_stat;
    int status = fstat(ffd, &file_stat);

    if (status != 0 || !S_ISREG(file_stat.st_mode)) {
        return_404(udata);
        close(ffd);
        return 0;
    }

    udata->file_fd = ffd;
    udata->file_size = file_stat.st_size;
    udata->file_stat = file_stat;

    return 0;
}

typedef bool (*header_handler)(yta_ctx*, const char*, std::size_t);

bool handle_if_modified_since(yta_ctx* ctx, const char* value, std::size_t) {
    user_data* udata = static_cast<user_data*>(ctx->user_data);

    tm stamp;
    if (strptime(value, yta::http::http_time_format(), &stamp) == NULL) {
        auto end = yta::http::serve_400(udata->response_buf);
        udata->response_size = end - udata->response_buf;
        udata->content = false;
        udata->finalized = true;
        return true;
    }
    time_t time = timegm(&stamp);

    // not modified
    if (time >= udata->file_stat.st_mtime) {
        auto end = yta::http::serve_304(udata->response_buf);
        udata->response_size = end - udata->response_buf;
        udata->content = false;
        udata->finalized = true;
        return true;
    }

    return false;
}

bool handle_range(yta_ctx* ctx, const char* value, std::size_t value_length) {
    user_data* udata = static_cast<user_data*>(ctx->user_data);

    // 7 bytes=X-
    if (value_length < 8) {
        return_400(udata);
        return true;
    }

    if (memcmp(value, "bytes=", 6) != 0) {
        return_400(udata);
        return true;
    }

    value = value + 6;

    auto sep = std::find(value, value + value_length, '-');

    if (sep == 0) {
        return_400(udata);
        return true;
    }

    if (sep - value == 0) {
        // this should be supported by rfc
        return_400(udata);
        return true;
    }

    *const_cast<char*>(sep) = 0; // UB
    int start_value = std::atoi(value);
    int end_value = 0;

    if (static_cast<std::size_t>(sep - value) == value_length - 1) {
        end_value = udata->file_stat.st_size;
    } else {
        end_value = std::atoi(sep + 1);
    }

    if (end_value <= start_value) {
        end_value = udata->file_stat.st_size;
    }

    if (start_value < 0) {
        start_value = 0;
    }

    if (end_value >= udata->file_stat.st_size) {
        end_value = udata->file_stat.st_size - 1;
    }

    udata->file_size = end_value - start_value + 1;
    udata->offset = start_value;

    auto end = yta::http::serve_206(udata->response_buf, udata->file_size,
                                    &udata->file_stat.st_mtime, start_value, end_value,
                                    udata->file_stat.st_size);
    udata->response_size = end - udata->response_buf;
    udata->content = true;
    udata->finalized = true;

    return true;
}

std::unordered_map<std::experimental::string_view, header_handler> header_callbacks{
    { "If-Modified-Since", handle_if_modified_since }, { "Range", handle_range }
};

int parse_headers(yta_ctx* ctx) {
    user_data* udata = static_cast<user_data*>(ctx->user_data);

    for (std::size_t i = 0; (i < udata->parser.num_headers) && !udata->finalized; ++i) {
        std::experimental::string_view header(udata->parser.headers[i].name,
                                              udata->parser.headers[i].name_len);
        auto it = header_callbacks.find(header);
        if (it != header_callbacks.end()) {
            auto done = it->second(ctx, udata->parser.headers[i].value,
                                   udata->parser.headers[i].value_len);
            if (done) {
                return 0;
            }
        }
    }

    if (!udata->finalized) {
        auto end = yta::http::serve_200(udata->response_buf, udata->file_size,
                                        &udata->file_stat.st_mtime);
        udata->response_size = end - udata->response_buf;
        udata->content = true;
        udata->finalized = true;
    }
    return 0;
}

void accept_logic(struct yta_ctx* ctx, struct user_data* udata);

yta_callback_status http_finish_callback(struct yta_ctx* ctx, void*, size_t) {
    struct user_data* udata = (struct user_data*)ctx->user_data;

    if (udata->file_fd != 0) {
        close(udata->file_fd);
        udata->file_fd = 0;
    }

    // uncork
    int enable = 0;
    if (setsockopt(ctx->fd, IPPROTO_TCP, TCP_CORK, &enable, sizeof(enable)) <
        0) {
        fprintf(stderr, "error setting TCP_CORK");
        exit(1);
    }

    accept_logic(ctx, udata);
    return YTA_OK;
}

yta_callback_status write_header_callback(yta_ctx* ctx, void*, size_t) {
    user_data* udata = static_cast<user_data*>(ctx->user_data);
    if (udata->content) {
        yta_async_sendfile(ctx, http_finish_callback, udata->file_fd, udata->file_size,
                           udata->offset);
        return YTA_OK;
    } else {
        return http_finish_callback(ctx, NULL, 0);
    }
}

yta_callback_status read_callback_http(yta_ctx* ctx, void* buf, size_t read) {
    user_data* udata = static_cast<user_data*>(ctx->user_data);

    std::size_t prev_count = udata->counter;
    udata->counter += read;

    int parsed = phr_parse_request(
        udata->buf, udata->counter, &udata->parser.method, &udata->parser.method_len,
        &udata->parser.path, &udata->parser.path_len, &udata->parser.minor_version,
        udata->parser.headers, &udata->parser.num_headers, prev_count);

    if (parsed == -1) {
        return_400(udata);
    } else if (parsed > 0) {
        parse_url(ctx, udata->parser.path, udata->parser.path_len);
        parse_headers(ctx);
    }

    if (udata->finalized) {
        // cork
        int enable = 1;
        if (setsockopt(ctx->fd, IPPROTO_TCP, TCP_CORK, &enable, sizeof(enable)) <
            0) {
            fprintf(stderr, "error setting TCP_CORK");
            exit(1);
        }

        yta_async_write(ctx, write_header_callback, udata->response_buf,
                        udata->response_size);
        return YTA_OK;
    }

    if (udata->counter == MAX_BUFFER_SIZE) {
        return http_finish_callback(ctx, NULL, 0);
    }

    yta_async_read(ctx, read_callback_http, (byte*)buf + read, MAX_BUFFER_SIZE - udata->counter);
    return YTA_OK;
}

yta_callback_status http_cleanup(struct yta_ctx* ctx) {
    struct user_data* udata = (struct user_data*)ctx->user_data;

    if (udata->file_fd != 0) {
        close(udata->file_fd);
        udata->file_fd = 0;
    }

    delete udata;

    return YTA_EXIT;
}

yta_callback_status timer_callback(yta_ctx* /* ctx */) {
    printf("Force closing http connection\n");
    return YTA_EXIT;
}

void accept_logic(yta_ctx* ctx, struct user_data* udata) {
    udata->parser = pico_parser{};
    udata->counter = 0;
    udata->finalized = false;
    udata->file_fd = 0;
    yta_async_read(ctx, read_callback_http, udata->buf, MAX_BUFFER_SIZE);
}

yta_callback_status accept_callback_http(struct yta_ctx* ctx) {
    struct user_data* udata = new user_data;
    ctx->user_data = udata;
    yta_set_close_callback(ctx, http_cleanup);
    yta_async_timer(ctx, timer_callback, 15, 0);
    accept_logic(ctx, udata);
    return YTA_OK;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s addr port\n", argv[0]);
        exit(1);
    }

    yta_run(argv[1], argv[2], accept_callback_http);

    return 0;
}
