cmake_minimum_required (VERSION 2.6)
project (yotta)

set (CMAKE_CXX_FLAGS "--std=c++17 -Wall -Werror -Wextra -pedantic -g -O3 -fsanitize=address -fno-omit-frame-pointer")

add_subdirectory(core)
add_subdirectory(http)
add_executable(yotta yotta.cpp)

target_link_libraries(yotta http_parser core http)

enable_testing()

