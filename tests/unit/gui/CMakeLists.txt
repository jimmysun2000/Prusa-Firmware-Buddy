# All these tests need buddy_utils
link_libraries(buddy_utils)

# define the test executable
add_executable(
  rectangle_tests ${CMAKE_SOURCE_DIR}/src/guiapi/src/Rect16.cpp
                  ${CMAKE_SOURCE_DIR}/tests/unit/test_main.cpp rectangle_tests.cpp
  )

add_executable(
  gui_tests ${CMAKE_CURRENT_SOURCE_DIR}/gui_tests.cpp
            ${CMAKE_SOURCE_DIR}/src/guiapi/src/numeric_input_config.cpp
  )

# define required search paths
target_include_directories(
  rectangle_tests
  PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/guiapi/include ${CMAKE_SOURCE_DIR}/src/gui
         ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/src/guiconfig
         ${CMAKE_SOURCE_DIR}/include
  )

target_include_directories(
  gui_tests
  PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/src/lang
         ${CMAKE_SOURCE_DIR}/src/guiapi/include ${CMAKE_CURRENT_SOURCE_DIR}
  )

# tell build system about the test case
add_catch_test(rectangle_tests)
add_catch_test(gui_tests)

# std_text_input_layout_tests
add_executable(std_text_input_layout_tests ${CMAKE_CURRENT_SOURCE_DIR}/text_input_layout_tests.cpp)
target_include_directories(
  std_text_input_layout_tests
  PUBLIC ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/src/gui
         ${CMAKE_SOURCE_DIR}/src/gui/dialogs ${CMAKE_SOURCE_DIR}/include
  )
target_compile_definitions(std_text_input_layout_tests PUBLIC BOARD=BOARD_XBUDDY)
add_catch_test(std_text_input_layout_tests)

# mini_text_input_layout_tests
add_executable(mini_text_input_layout_tests ${CMAKE_CURRENT_SOURCE_DIR}/text_input_layout_tests.cpp)
target_include_directories(
  mini_text_input_layout_tests
  PUBLIC ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/src/gui
         ${CMAKE_SOURCE_DIR}/src/gui/dialogs ${CMAKE_SOURCE_DIR}/include
  )
target_compile_definitions(mini_text_input_layout_tests PUBLIC BOARD=BOARD_BUDDY)
add_catch_test(mini_text_input_layout_tests)

# color_tests
add_executable(
  color_tests
  ${CMAKE_CURRENT_SOURCE_DIR}/color_tests.cpp ${CMAKE_SOURCE_DIR}/src/common/utils/color.cpp
  ${CMAKE_SOURCE_DIR}/src/common/str_utils.cpp
  )
target_include_directories(
  color_tests PUBLIC ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/src/lang
                     ${CMAKE_SOURCE_DIR}/include
  )
add_catch_test(color_tests)

# maintenance_reminder_math_tests
add_executable(
  maintenance_reminder_math_tests ${CMAKE_CURRENT_SOURCE_DIR}/maintenance_reminder_math_tests.cpp
  )
target_include_directories(maintenance_reminder_math_tests PUBLIC ${CMAKE_SOURCE_DIR}/src)
add_catch_test(maintenance_reminder_math_tests)

# user_macro_tests
add_executable(user_macro_tests ${CMAKE_CURRENT_SOURCE_DIR}/user_macro_tests.cpp)
target_include_directories(user_macro_tests PUBLIC ${CMAKE_SOURCE_DIR}/src)
add_catch_test(user_macro_tests)

add_subdirectory(lazyfilelist)
add_subdirectory(jogwheel)
add_subdirectory(window)
add_subdirectory(window_file_list)
add_subdirectory(neopixel)
