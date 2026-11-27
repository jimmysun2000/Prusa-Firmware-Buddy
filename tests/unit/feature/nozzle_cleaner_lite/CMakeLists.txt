add_executable(
  cleaner_zigzag_tests ${CMAKE_SOURCE_DIR}/src/feature/nozzle_cleaner_lite/cleaner_zigzag.cpp
                       cleaner_zigzag_tests.cpp
  )
target_include_directories(cleaner_zigzag_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_catch_test(cleaner_zigzag_tests)

add_executable(
  custom_wiper_geometry_tests
  ${CMAKE_SOURCE_DIR}/src/feature/nozzle_cleaner_lite/cleaner_zigzag.cpp
  ${CMAKE_SOURCE_DIR}/src/feature/nozzle_cleaner_lite/custom_wiper_geometry.cpp
  custom_wiper_geometry_tests.cpp
  )
target_include_directories(custom_wiper_geometry_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_catch_test(custom_wiper_geometry_tests)
