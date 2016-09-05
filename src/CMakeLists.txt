cmake_minimum_required (VERSION 2.6)
project (yotta)

set (CMAKE_CXX_FLAGS "--std=c++1z -Wall -Werror -Wextra -pedantic -g -O3 -fsanitize=address -fno-omit-frame-pointer -fstack-protector-strong -fpie -Wl,-z,relro,-z,now")

add_subdirectory(core)
add_subdirectory(http)
add_executable(yotta yotta.cpp picohttpparser/picohttpparser.c)

target_link_libraries(yotta core http jemalloc)

enable_testing()

