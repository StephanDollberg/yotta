cmake_minimum_required (VERSION 2.6)
project (yotta)

set (CMAKE_CXX_FLAGS "--std=c++1y -Wall -Werror -Wextra -pedantic -g -O3 -fno-omit-frame-pointer -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fpie -pie -Wl,-z,relro,-z,now")
set (CMAKE_C_FLAGS "--std=gnu11 -g -O3 -Wall -Werror -Wextra -pedantic -fno-omit-frame-pointer -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fpic -Wl,-z,relro,-z,now")

add_subdirectory(core)
add_subdirectory(http)
add_executable(yotta yotta.cpp picohttpparser/picohttpparser.c)

target_link_libraries(yotta core http)

enable_testing()

add_test(NAME integration_test
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/run_integration_test.sh $<TARGET_FILE:yotta>
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test/)

install(TARGETS yotta DESTINATION /usr/local/bin)
