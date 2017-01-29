#include <algorithm>
#include <experimental/string_view>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>

#include <sys/stat.h>
#include <unistd.h>

#include "core/yta_event_loop.h"
#include "http/yta_http.hpp"

#include "picohttpparser/picohttpparser.h"

const std::size_t MAX_URL_SIXE = 512;
const std::size_t MAX_HEADERS = 20;
const std::size_t MAX_BUFFER_SIZE = 1024;

typedef unsigned char byte;

struct parser_data {
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

    std::size_t response_size;
    bool content;
    std::experimental::string_view extension;

    int file_fd;
    std::size_t file_size;
    std::size_t offset = 0;

    parser_data parser;

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

std::unordered_map<std::experimental::string_view, std::experimental::string_view> mime_types {
    { ".html", "text/html" }, { ".css", "text/css" }, { ".png", "image/png"}, { ".jpg", "image/jpeg" },
    { ".jpeg", "image/jpeg"}, { ".txt", "text/plain" }, {".js", "application/x-javascript"},
    { ".json", "application/json"}, { ".pdf", "application/pdf" }, { ".zip", "application/zip" },
    { ".woff", "application/font-woff" }, { ".woff2", "application/font-woff2" },
    { ".opus", "audio/opus"}, { ".mp4", "video/mp4"}, { ".mpeg", "video/mpeg" }, { ".mpg", "video/mpeg" },
    { ".mp3", "audio/mpeg"}
};

int parse_url(yta_ctx* ctx, const char* path, size_t length) {
    user_data* udata = static_cast<user_data*>(ctx->user_data);

    // parse query string
    auto pos = std::find(path, path + length, '?');
    if (pos != path + length) {
        length = pos - path;
    }

    // clean path buffer is MAX_URL_SICE
    // leave space for appended index.html
    if ((length > (MAX_URL_SIXE - 20)) || (length == 0)) {
        return_404(udata);
        return 0;
    }

    char normalized_path[MAX_URL_SIXE] = { 0 };
    // prepend . for current directory
    normalized_path[0] = '.';
    std::size_t new_length = 1;

    // clean path
    new_length += yta::http::clean_path(path, length, normalized_path + 1);

	// clean_path removes trailing slash except for root;
    // put the trailing slash back if necessary.
    if (path[length - 1] == '/' && new_length != 2) {
        normalized_path[new_length] = '/';
        ++new_length;
    }

    // append index.html to urls with trailing slashes
    if (normalized_path[new_length - 1] == '/') {
        const char index_html[] = "index.html";
        std::copy(index_html, index_html + 10, normalized_path + new_length);
        new_length += 10;
    }

    int ffd = open(normalized_path, O_RDONLY);

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

    std::experimental::string_view path_view(normalized_path, new_length);
    auto path_begin = path_view.rfind('.');

    // we always find . at the beginning because of ./FILENAME
    if (path_begin != 0) {
        auto it = mime_types.find(path_view.substr(path_begin));
        if (it != mime_types.end()) {
            udata->extension = it->second;
        } else {
            udata->extension = { "text/html" };
        }
    } else {
        udata->extension = { "text/html" };
    }

    udata->file_fd = ffd;
    udata->file_size = file_stat.st_size;
    udata->file_stat = file_stat;

    return 0;
}

typedef bool (*header_handler)(yta_ctx*, std::experimental::string_view);

bool handle_if_modified_since(yta_ctx* ctx, std::experimental::string_view value) {
    user_data* udata = static_cast<user_data*>(ctx->user_data);

    tm stamp;
    if (strptime(value.data(), yta::http::http_time_format(), &stamp) == NULL) {
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

bool handle_range(yta_ctx* ctx, std::experimental::string_view value) {
    user_data* udata = static_cast<user_data*>(ctx->user_data);

    // 7 bytes=X-
    if (value.size() < 8) {
        return_400(udata);
        return true;
    }

    auto iter = value.begin();

    if (!std::equal(iter, iter + 6, "bytes=")) {
        return_400(udata);
        return true;
    }

    std::advance(iter, 6);

    auto sep = std::find(iter, value.end(), '-');

    if (sep == value.end()) {
        return_400(udata);
        return true;
    }

    // Range: bytes=-123
    if (sep == iter) {
        // this should be supported by rfc
        return_400(udata);
        return true;
    }

    auto val = std::string(iter, sep);
    int start_value = std::stoi(val);
    int end_value = 0;

    if (std::next(sep, 1) == value.end()) {
        end_value = udata->file_stat.st_size - 1;
    } else {
        auto val = std::string(sep + 1, value.end());
        end_value = std::stoi(val);
    }

    // sanity checks
    if (end_value <= start_value) {
        end_value = udata->file_stat.st_size - 1;
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
            std::experimental::string_view value(udata->parser.headers[i].value,
                                                  udata->parser.headers[i].value_len);
            auto done = it->second(ctx, value);
            if (done) {
                return 0;
            }
        }
    }

    if (!udata->finalized) {
        auto end = yta::http::serve_200(udata->response_buf, udata->file_size,
                                        &udata->file_stat.st_mtime, udata->extension);
        udata->response_size = end - udata->response_buf;
        udata->content = true;
        udata->finalized = true;
    }
    return 0;
}

void accept_logic(struct yta_ctx* ctx, struct user_data* udata);

yta_callback_status http_finish_callback(struct yta_ctx* ctx, void*, size_t) {
    struct user_data* udata = static_cast<user_data*>(ctx->user_data);

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
    struct user_data* udata = static_cast<struct user_data*>(ctx->user_data);

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
    udata->parser = parser_data{};
    udata->counter = 0;
    udata->finalized = false;
    udata->file_fd = 0;
    yta_async_read(ctx, read_callback_http, udata->buf, MAX_BUFFER_SIZE);
}

yta_callback_status accept_callback_http(struct yta_ctx* ctx) {
    struct user_data* udata = new user_data;
    if (udata == nullptr) {
        return YTA_EXIT;
    }
    ctx->user_data = udata;
    yta_set_close_callback(ctx, http_cleanup);
    yta_async_timer(ctx, timer_callback, 300, 0);
    accept_logic(ctx, udata);
    return YTA_OK;
}


int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s addr port\n", argv[0]);
        exit(1);
    }

    char default_pidfile[] =  { "/tmp/yotta.pid" };
    char* pidfile_path = default_pidfile;
    int daemonize = 0;

    if (argc == 4) {
        pidfile_path = argv[3];
    }

    if (argc == 5) {
        pidfile_path = argv[3];
        daemonize = atoi(argv[4]);
    }

    yta_run(argv, argv[1], argv[2], pidfile_path, daemonize, accept_callback_http);

    return 0;
}
