#include "yta_http.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <ctime>

namespace yta {
namespace http {

char* serve_200(char* buf, std::size_t content_length, time_t* last_modified, std::experimental::string_view content_type) {
    const char base_buf[] = "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\n";
    std::size_t base_buf_size = sizeof(base_buf) - 1;

    const char last_modified_buf[] = "Last-Modified: ";
    std::size_t last_modified_size = sizeof(last_modified_buf) - 1;
    const char content_length_buf[] = "\r\nContent-Length: ";
    std::size_t content_length_buf_size = sizeof(content_length_buf) - 1;
    const char content_type_buf[] = "\r\nContent-Type: ";
    std::size_t content_type_buf_size = sizeof(content_type_buf) - 1;

    const char linefeed[] = "\r\n\r\n";

    buf = std::copy(base_buf, base_buf + base_buf_size, buf);
    buf = std::copy(last_modified_buf, last_modified_buf + last_modified_size, buf);
    buf += std::strftime(buf, 512, http_time_format(), gmtime(last_modified));
    buf = std::copy(content_length_buf, content_length_buf + content_length_buf_size,
                       buf);
    buf += std::sprintf(buf, "%lu", content_length);
    buf = std::copy(content_type_buf, content_type_buf + content_type_buf_size,
                       buf);
    buf = std::copy(content_type.begin(), content_type.end(), buf);
    buf = std::copy(linefeed, linefeed + 4, buf);

    return buf;
}

char* serve_304(char* buf) {
    const char result[] = "HTTP/1.1 304 Not Modified\r\nConnection: "
                          "keep-alive\r\nContent-Length: 0\r\n\r\n";
    return std::copy(result, result + sizeof(result) - 1, buf);
}

char* serve_206(char* buf, std::size_t content_length,
                time_t* last_modified, std::size_t range_start, std::size_t range_end, std::size_t file_size) {
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
        std::strftime(buf, 512, http_time_format(), gmtime(last_modified));
    buf = std::copy(content_range_buf, content_range_buf + content_range_buf_size, buf);
    buf += std::sprintf(buf, "%lu", range_start);
    *(buf++) = '-';
    buf += std::sprintf(buf, "%lu", range_end);
    *buf++ = '/';
    buf += std::sprintf(buf, "%lu", file_size);
    buf = std::copy(content_length_buf, content_length_buf + content_length_buf_size,
                       buf);
    buf += std::sprintf(buf, "%lu", content_length);

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

// translated from Go source
// Cleans path from .. etc; returns new path size
std::size_t clean_path(const char* path, std::size_t length, char* const normalized_path) {
    char* output = normalized_path;

    if (length == 0) {
        *output = '.';
        return 1;
    }

    std::size_t r = 0;
    std::size_t dotdot = 0;

    bool rooted = (path[0] == '/');
    if (rooted) {
        *output++ = '/';
        r = 1;
        dotdot = 1;
    }

    while (r < length) {
        if (path[r] == '/') {
            ++r;
        } else if (path[r] == '.' && (r+1 == length || path[r+1] == '/')) {
            ++r;
        } else if (path[r] == '.' && path[r+1] == '.' && (r+2 == length || path[r+2] == '/')) {
            r += 2;

            if (std::size_t(output - normalized_path) > dotdot) {
                --output;
                while (std::size_t(output - normalized_path) > dotdot && *output != '/') {
                    --output;
                }
            } else if(!rooted) {
                if (output - normalized_path > 0) {
                    *output++ = '/';
                }
                *output++ = '.';
                *output++ = '.';
                dotdot = output - &normalized_path[0];
            }
        } else {
            if ((rooted && (std::size_t(output - normalized_path) != 1)) || (!rooted && (output - normalized_path != 0))) {
                *output++ = '/';
            }

            for(; r < length && path[r] != '/'; ++r) {
                *output++ = path[r];
            }
        }
    }

    if (output - normalized_path == 0) {
        *output++ = '.';
    }

    return output - normalized_path;
}

}
}
