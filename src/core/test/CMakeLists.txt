include_directories(${TEST_SOURCE_DIR}/..)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")

add_executable(core_test core_test.cpp)
target_link_libraries(core_test core)
