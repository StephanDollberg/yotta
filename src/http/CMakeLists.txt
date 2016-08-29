cmake_minimum_required (VERSION 2.6)
project (yotta_http)

set (CMAKE_CXX_FLAGS "--std=c++11 -Wall -Werror -Wextra -pedantic -O3 -fno-omit-frame-pointer")

add_library(http yta_http.cpp)

enable_testing()
add_subdirectory(test)

add_test(NAME http_test COMMAND http_test)
