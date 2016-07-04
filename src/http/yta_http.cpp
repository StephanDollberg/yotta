#include "yta_http.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
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

namespace yta {
namespace http {

char* serve_200(char* buf, int content_length, time_t* last_modified) {
    const char base_buf[] = "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\n";
    std::size_t base_buf_size = sizeof(base_buf) - 1;

    const char last_modified_buf[] = "Last-Modified: ";
    std::size_t last_modified_size = sizeof(last_modified_buf) - 1;
    const char content_length_buf[] = "\r\nContent-Length: ";
    std::size_t content_length_buf_size = sizeof(content_length_buf) - 1;

    const char linefeed[] = "\r\n\r\n";

    buf = std::copy(base_buf, base_buf + base_buf_size, buf);
    buf = std::copy(last_modified_buf, last_modified_buf + last_modified_size, buf);
    buf += strftime(buf, 512, http_time_format(), gmtime(last_modified));
    buf = std::copy(content_length_buf, content_length_buf + content_length_buf_size,
                       buf);
    buf += sprintf(buf, "%d", content_length);
    buf = std::copy(linefeed, linefeed + 4, buf);

    return buf;
}

char* serve_304(char* buf) {
    const char result[] = "HTTP/1.1 304 Not Modified\r\nConnection: "
                          "keep-alive\r\nContent-Length: 0\r\n\r\n";
    return std::copy(result, result + sizeof(result) - 1, buf);
}

char* serve_206(char* buf, int content_length,
                time_t* last_modified, int range_start, int range_end, int file_size) {
    const char base_buf[] = "HTTP/1.1 206 Partial Content\r\nAccept-Ranges: bytes";
    std::size_t base_buf_size = sizeof(base_buf) - 1;

    const char last_modified_buf[] = "\r\nLast-Modified: ";
    std::size_t last_modified_size = sizeof(last_modified_buf) - 1;

    const char content_range_buf[] = "\r\nContent-Range: bytes ";
    std::size_t content_range_buf_size = sizeof(content_range_buf) - 1;

        const char content_length_buf[] = "\r\nContent-Length: ";
    std::size_t content_length_buf_size = sizeof(content_length_buf) - 1;

    const char linefeed[] = "\r\n\r\n";

    buf = std::copy(base_buf, base_buf + base_buf_size, buf);

    buf = std::copy(last_modified_buf, last_modified_buf + last_modified_size, buf);
    buf +=
        strftime(buf, 512, http_time_format(), gmtime(last_modified));
    buf = std::copy(content_range_buf, content_range_buf + content_range_buf_size, buf);
    buf += sprintf(buf, "%d", range_start);
    *(buf++) = '-';
    buf += sprintf(buf, "%d", range_end);
    *buf++ = '/';
    buf += sprintf(buf, "%d", file_size);
    buf = std::copy(content_length_buf, content_length_buf + content_length_buf_size,
                       buf);
    buf += sprintf(buf, "%d", content_length);

    buf = std::copy(linefeed, linefeed + 4, buf);

    return buf;
}

char* serve_400(char* buf) {
    const char result[] = "HTTP/1.1 400 Bad Request\r\nConnection: "
                          "close\r\nContent-Length: 0\r\n\r\n";
    return std::copy(result, result + sizeof(result) - 1, buf);
}

char* serve_404(char* buf) {
    const char result[] = "HTTP/1.1 404 Not Found\r\nConnection: "
                          "keep-alive\r\nContent-Length: 0\r\n\r\n";
    return std::copy(result, result + sizeof(result) - 1, buf);
}
}
}
