cmake_minimum_required (VERSION 2.6)
project (yotta)

set (CMAKE_CXX_FLAGS "--std=c++1y -Wall -Werror -Wextra -pedantic -g -O3 -fsanitize=address -fno-omit-frame-pointer -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fpie -pie -Wl,-z,relro,-z,now")

add_subdirectory(core)
add_subdirectory(http)
add_executable(yotta yotta.cpp picohttpparser/picohttpparser.c)

target_link_libraries(yotta core http)

enable_testing()

install(TARGETS yotta DESTINATION /usr/local/bin)

