#ifndef YOTTA_HTTP_HPP
#define YOTTA_HTTP_HPP

#include <time.h>
#include <cstddef>

#include <experimental/string_view>

namespace yta {
namespace http {

char* serve_200(char* buf, std::size_t content_length, time_t* last_modified, std::experimental::string_view extension);

char* serve_206(char* buf, std::size_t content_length,
                time_t* last_modified, std::size_t range_start, std::size_t range_end, std::size_t file_size);

char* serve_304(char* buf);

char* serve_400(char* buf);

char* serve_404(char* buf);

std::size_t clean_path(char* buf, std::size_t length);

constexpr const char* http_time_format() {
    return "%a, %d %b %Y %H:%M:%S %Z";
}

} // namespace http
} // namespace yta


#endif // YOTTA_HTTP_HPP
