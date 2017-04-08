include_directories(${TEST_SOURCE_DIR}/..)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")

add_executable(http_test http_test.cpp)
target_link_libraries(http_test http)
