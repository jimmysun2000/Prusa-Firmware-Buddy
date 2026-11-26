add_executable(
  side_strip_status_tests ${CMAKE_CURRENT_SOURCE_DIR}/side_strip_status_tests.cpp
                          ${CMAKE_SOURCE_DIR}/src/leds/side_strip_status.cpp
  )
target_include_directories(side_strip_status_tests PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(side_strip_status_tests buddy_utils)
add_catch_test(side_strip_status_tests)
