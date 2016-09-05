cmake_minimum_required (VERSION 2.6)
project (yotta_http)

set (CMAKE_CXX_FLAGS "--std=c++1z -Wall -Werror -Wextra -pedantic -O3 -fsanitize=address -fno-omit-frame-pointer -fstack-protector-strong -fpie -Wl,-z,relro,-z,now")

add_library(http yta_http.cpp)

enable_testing()
add_subdirectory(test)

add_test(NAME http_test COMMAND http_test)
