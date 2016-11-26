include_directories(${TEST_SOURCE_DIR}/..)

add_executable(core_test core_test.cpp)
target_link_libraries(core_test core)
