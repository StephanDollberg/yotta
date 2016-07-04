#ifndef YOTTA_HTTP_HPP
#define YOTTA_HTTP_HPP

#include <time.h>

namespace yta {
namespace http {

char* serve_200(char* buf, int content_length, time_t* last_modified);

char* serve_206(char* buf, int content_length,
                time_t* last_modified, int range_start, int range_end, int file_size);

char* serve_304(char* buf);

char* serve_400(char* buf);

char* serve_404(char* buf);

constexpr const char* http_time_format() {
    return "%a, %d %b %Y %H:%M:%S %Z";
}

} // namespace http
} // namespace yta


#endif // YOTTA_HTTP_HPP
