cmake_minimum_required (VERSION 2.6)
project (yotta_core)

add_library(core SHARED yta_event_loop.c yta_process.c)

enable_testing()
add_subdirectory(test)
add_test(NAME core_test COMMAND core_test)

install(TARGETS core DESTINATION /usr/local/lib)
