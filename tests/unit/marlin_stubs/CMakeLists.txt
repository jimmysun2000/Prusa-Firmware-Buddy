add_executable(slow_load_pattern_tests slow_load_pattern_tests.cpp)
target_include_directories(
  slow_load_pattern_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/marlin_stubs/include
  )
add_catch_test(slow_load_pattern_tests)
