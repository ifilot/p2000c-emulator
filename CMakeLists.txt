cmake_minimum_required(VERSION 3.24)

project(p2000c_emulator VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(P2000C_BUILD_APP "Build the Qt desktop application" ON)

add_library(z80_core STATIC
  third_party/superzazu_z80/z80.c
  third_party/superzazu_z80/z80.h
)
target_include_directories(z80_core PUBLIC third_party/superzazu_z80)

add_library(p2000c_core STATIC
  src/core/p2000c_machine.cpp
  src/core/p2000c_machine.h
  src/core/raw_disk_image.cpp
  src/core/raw_disk_image.h
  src/core/terminal.cpp
  src/core/terminal.h
)
target_include_directories(p2000c_core PUBLIC src)
target_link_libraries(p2000c_core PRIVATE z80_core)
target_compile_options(p2000c_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(p2000c_media_info tools/media_info.cpp)
target_link_libraries(p2000c_media_info PRIVATE p2000c_core)

if(P2000C_BUILD_APP)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
  qt_standard_project_setup()

  qt_add_executable(p2000c
    src/app/display_widget.cpp
    src/app/display_widget.h
    src/app/main.cpp
    src/app/main_window.cpp
    src/app/main_window.h
    src/app/screen_color_dialog.cpp
    src/app/screen_color_dialog.h
  )
  qt_add_resources(p2000c "p2000c_assets"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
      "font/P2000C font mini.png"
      "images/blank.hda"
      "images/chess.flp"
      "images/ipldump.flp"
      "images/system.flp"
      "images/zork.flp"
      "tools/IPLDUMP.BIN"
  )
  target_link_libraries(p2000c PRIVATE p2000c_core Qt6::Widgets)
  set_target_properties(p2000c PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
  )
endif()

if(BUILD_TESTING)
  add_test(
    NAME ipldump_format_test
    COMMAND ${CMAKE_COMMAND}
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tools/IPLDUMP.ASM
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_ipldump.cmake
  )

  add_executable(raw_disk_image_test tests/raw_disk_image_test.cpp)
  target_link_libraries(raw_disk_image_test PRIVATE p2000c_core)
  add_test(
    NAME raw_disk_image_test
    COMMAND raw_disk_image_test
            ${CMAKE_CURRENT_SOURCE_DIR}/images/system.flp
            ${CMAKE_CURRENT_SOURCE_DIR}/images/blank.hda
  )

  add_executable(p2000c_machine_test tests/p2000c_machine_test.cpp)
  target_link_libraries(p2000c_machine_test PRIVATE p2000c_core)
  add_test(
    NAME p2000c_machine_test
    COMMAND p2000c_machine_test
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/IPLDUMP.BIN
            ${CMAKE_CURRENT_SOURCE_DIR}/images/system.flp
            ${CMAKE_CURRENT_SOURCE_DIR}/images/zork.flp
            ${CMAKE_CURRENT_SOURCE_DIR}/images/chess.flp
            ${CMAKE_CURRENT_SOURCE_DIR}/images/ipldump.flp
            ${CMAKE_CURRENT_SOURCE_DIR}/images/blank.hda
  )

  if(P2000C_BUILD_APP)
    qt_add_executable(p2000c_app_test
      tests/main_window_test.cpp
      src/app/display_widget.cpp
      src/app/display_widget.h
      src/app/main_window.cpp
      src/app/main_window.h
      src/app/screen_color_dialog.cpp
      src/app/screen_color_dialog.h
    )
    qt_add_resources(p2000c_app_test "p2000c_test_assets"
      PREFIX "/"
      BASE "${CMAKE_CURRENT_SOURCE_DIR}"
      FILES
        "font/P2000C font mini.png"
        "images/blank.hda"
        "images/chess.flp"
        "images/ipldump.flp"
        "images/system.flp"
        "images/zork.flp"
        "tools/IPLDUMP.BIN"
    )
    target_link_libraries(p2000c_app_test PRIVATE p2000c_core Qt6::Widgets)
    add_test(
      NAME p2000c_app_test
      COMMAND p2000c_app_test ${CMAKE_CURRENT_BINARY_DIR}/test_app_data
    )
    set_tests_properties(p2000c_app_test PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
  endif()
endif()
