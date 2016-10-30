cmake_minimum_required (VERSION 2.6)
project (yotta_http)

set (CMAKE_CXX_FLAGS "--std=c++1y -Wall -Werror -Wextra -pedantic -O3 -fsanitize=address -fno-omit-frame-pointer -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fpic -Wl,-z,relro,-z,now")

add_library(http SHARED yta_http.cpp)

enable_testing()
add_subdirectory(test)

add_test(NAME http_test COMMAND http_test)
