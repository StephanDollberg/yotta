#include <algorithm>
#include <experimental/string_view>
#include <iostream>
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

#include <http_parser.h>

#include "core/yta_event_loop.h"
#include "http/yta_http.hpp"

typedef unsigned char byte;

struct line {
    char* field;
    size_t field_len;
    char* value;
    size_t value_len;
};

struct user_data {
    int counter;
    int unparsed;

    char buf[512];
    char response_buf[512];

    bool finalized;

    int response_size;
    bool content;

    int file_fd;
    int file_size;
    int offset = 0;

    http_parser parser;
    http_parser_settings parser_settings;

    struct stat file_stat;

    // header parsing logic taken from https://gist.github.com/ry/155877 for now
    struct line header[20];
    int nlines = 0;
    int last_was_value = 1;
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

int parser_header_field_callback(http_parser* parser, const char* at, size_t len) {
    yta_ctx* ctx = static_cast<yta_ctx*>(parser->data);
    user_data* udata = static_cast<user_data*>(ctx->user_data);

    if (udata->last_was_value) {
        if (udata->nlines >= 20) {
            return 0;
        }; // error!

        udata->nlines++;


        udata->header[udata->nlines - 1].value = NULL;
        udata->header[udata->nlines - 1].value_len = 0;

        udata->header[udata->nlines - 1].field_len = len;
        udata->header[udata->nlines - 1].field = (char*)at;

    } else {
        udata->header[udata->nlines - 1].field_len += len;
    }

    udata->header[udata->nlines - 1].field[udata->header[udata->nlines - 1].field_len] =
        '\0';
    udata->last_was_value = 0;

    return 0;
}

int parser_value_field_callback(http_parser* parser, const char* at, size_t len) {
    struct yta_ctx* ctx = static_cast<yta_ctx*>(parser->data);
    struct user_data* udata = static_cast<user_data*>(ctx->user_data);

    if (!udata->last_was_value) {
        if (udata->nlines >= 20) {
            return 0;
        }

        udata->header[udata->nlines - 1].value_len = len;
        udata->header[udata->nlines - 1].value = (char*)at;
    } else {
        udata->header[udata->nlines - 1].value_len += len;
    }

    udata->header[udata->nlines - 1].value[udata->header[udata->nlines - 1].value_len] =
        '\0';
    udata->last_was_value = 1;

    return 0;
}

int parser_url_callback(http_parser* parser, const char* at, size_t length) {
    struct yta_ctx* ctx = static_cast<yta_ctx*>(parser->data);
    struct user_data* udata = static_cast<user_data*>(ctx->user_data);

    *const_cast<char*>(at + length) = '\0'; // UB

    int ffd = open(at + 1, O_RDONLY);

    if (ffd == -1) {
        return_404(udata);
        return 0;
    }

    struct stat file_stat;
    int status = fstat(ffd, &file_stat);

    if (status != 0) {
        return_404(udata);
        close(ffd);
        return 0;
    }

    udata->file_fd = ffd;
    udata->file_size = file_stat.st_size;
    udata->file_stat = file_stat;

    return 0;
}

typedef bool (*header_handler)(yta_ctx*, char*, std::size_t);

bool handle_if_modified_since(yta_ctx* ctx, char* value, std::size_t) {
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

bool handle_range(yta_ctx* ctx, char* value, std::size_t value_length) {
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

    *sep = 0;
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

int parser_complete_callback(http_parser* parser) {
    yta_ctx* ctx = static_cast<yta_ctx*>(parser->data);
    user_data* udata = static_cast<user_data*>(ctx->user_data);

    for (int i = 0; (i < udata->nlines) && !udata->finalized; ++i) {
        std::experimental::string_view header(udata->header[i].field,
                                              udata->header[i].field_len);
        auto it = header_callbacks.find(header);
        if (it != header_callbacks.end()) {
            auto done =
                it->second(ctx, udata->header[i].value, udata->header[i].value_len);
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

void http_finish_callback(struct yta_ctx* ctx, void*, size_t) {
    // struct user_data* udata = (struct user_data*)ctx->user_data;
    // yta_async_read(ctx, printer, udata->buf, 512);
    struct user_data* udata = (struct user_data*)ctx->user_data;

    if (udata->file_fd != 0) {
        close(udata->file_fd);
        udata->file_fd = 0;
    }

    // delete udata;
    // yta_close_context(ctx);

    accept_logic(ctx, udata);
}

void write_header_callback(struct yta_ctx* ctx, void*, size_t) {
    struct user_data* udata = (struct user_data*)ctx->user_data;
    if (udata->content) {
        yta_async_sendfile(ctx, http_finish_callback, udata->file_fd, udata->file_size,
                           udata->offset);
    } else {
        http_finish_callback(ctx, NULL, 0);
    }
}

void read_callback_http(struct yta_ctx* ctx, void* buf, size_t read) {
    struct user_data* udata = (struct user_data*)ctx->user_data;

    udata->counter += read;

    int parsed = http_parser_execute(&udata->parser, &udata->parser_settings,
                                     (char*)buf - udata->unparsed, read);

    udata->unparsed = read + udata->unparsed - parsed;

    if (udata->finalized) {
        yta_async_write(ctx, write_header_callback, udata->response_buf,
                        udata->response_size);
    } else {
        yta_async_read(ctx, read_callback_http, (byte*)buf + read, 512 - udata->counter);
    }
}

void http_cleanup(struct yta_ctx* ctx) {
    struct user_data* udata = (struct user_data*)ctx->user_data;

    if (udata->file_fd != 0) {
        close(udata->file_fd);
        udata->file_fd = 0;
    }

    delete udata;
}

void accept_logic(struct yta_ctx* ctx, struct user_data* udata) {
    http_parser_init(&udata->parser, HTTP_REQUEST); /* initialise parser */
    udata->parser.data = ctx;
    http_parser_settings_init(&udata->parser_settings);
    udata->parser_settings.on_url = parser_url_callback;
    udata->parser_settings.on_message_complete = parser_complete_callback;
    udata->parser_settings.on_header_field = parser_header_field_callback;
    udata->parser_settings.on_header_value = parser_value_field_callback;

    udata->counter = 0;
    udata->unparsed = 0;
    udata->finalized = false;
    udata->file_fd = 0;
    udata->nlines = 0;
    udata->last_was_value = 1;
    yta_async_read(ctx, read_callback_http, udata->buf, 512);
}

void accept_callback_http(struct yta_ctx* ctx) {
    struct user_data* udata = new user_data;
    ctx->user_data = udata;
    yta_set_close_callback(ctx, http_cleanup);
    accept_logic(ctx, udata);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s addr port\n", argv[0]);
        exit(1);
    }

    yta_run(argv[1], argv[2], accept_callback_http);

    return 0;
}
