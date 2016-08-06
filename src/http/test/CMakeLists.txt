include_directories(${TEST_SOURCE_DIR}/..)

add_executable(http_test http_test.cpp)
target_link_libraries(http_test http)
