cmake_minimum_required (VERSION 2.6)
project (yotta_core)

set (CMAKE_C_FLAGS "--std=gnu11 -g -O3 -Wall -Werror -Wextra -pedantic -fsanitize=address -fno-omit-frame-pointer -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fpic -Wl,-z,relro,-z,now")

add_library(core SHARED yta_event_loop.c yta_process.c)

#enable_testing()
#add_subdirectory(test)
#add_test(NAME core_test COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test/core_test.sh)
