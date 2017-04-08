cmake_minimum_required (VERSION 2.6)
project (yotta_http)

add_library(http SHARED yta_http.cpp)

enable_testing()
add_subdirectory(test)

add_test(NAME http_test COMMAND http_test)

install(TARGETS http DESTINATION /usr/local/lib)
