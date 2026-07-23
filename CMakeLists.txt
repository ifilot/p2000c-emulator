cmake_minimum_required(VERSION 3.24)

# Release-version source of truth. Application metadata, About text, package
# names, installer metadata, and CI release names are derived from this value.
project(p2000c_emulator VERSION 0.2.0 LANGUAGES C CXX)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/package-version.txt"
     "${PROJECT_VERSION}\n")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(P2000C_BUILD_APP "Build the Qt desktop application" ON)

add_library(z80_core STATIC
  third_party/superzazu_z80/z80.c
  third_party/superzazu_z80/z80.h
)
target_include_directories(z80_core PUBLIC third_party/superzazu_z80)

add_library(i8088_core STATIC
  third_party/blink16_8086/i8088.cpp
  third_party/blink16_8086/i8088.h
)
target_include_directories(i8088_core PUBLIC third_party/blink16_8086)

add_library(p2000c_core STATIC
  src/core/copower_board.cpp
  src/core/copower_board.h
  src/core/p2000c_machine.cpp
  src/core/p2000c_machine.h
  src/core/raw_disk_image.cpp
  src/core/raw_disk_image.h
  src/core/terminal.cpp
  src/core/terminal.h
)
target_include_directories(p2000c_core PUBLIC src)
target_link_libraries(p2000c_core PRIVATE i8088_core z80_core)
target_compile_options(p2000c_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_library(p2000c_app_logic STATIC
  src/app/fast_boot_controller.cpp
  src/app/fast_boot_controller.h
)
target_include_directories(p2000c_app_logic PUBLIC src)
target_compile_options(p2000c_app_logic PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(p2000c_media_info tools/media_info/media_info.cpp)
target_link_libraries(p2000c_media_info PRIVATE p2000c_core)

add_executable(p2000c_imd_to_flp tools/imd_to_flp/imd_to_flp.cpp)
target_link_libraries(p2000c_imd_to_flp PRIVATE p2000c_core)
install(TARGETS p2000c_imd_to_flp
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT application
)

add_executable(p2000c_cli tools/p2000c_cli/p2000c_cli.cpp)
target_link_libraries(p2000c_cli PRIVATE p2000c_core)
install(TARGETS p2000c_cli
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT application
)

if(P2000C_BUILD_APP)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
  if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
    find_package(OpenAL CONFIG REQUIRED)
  else()
    find_package(OpenAL REQUIRED)
  endif()
  qt_standard_project_setup()

  qt_add_executable(p2000c
    src/app/display_widget.cpp
    src/app/display_widget.h
    src/app/fast_boot_controller.h
    src/app/main.cpp
    src/app/hardware_audio.cpp
    src/app/hardware_audio.h
    src/app/main_window.cpp
    src/app/main_window.h
    src/app/screen_color_dialog.cpp
    src/app/screen_color_dialog.h
  )
  qt_add_resources(p2000c "p2000c_assets"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
      "audio/525_seek_6ms.wav"
      "audio/525_spin_end.wav"
      "audio/525_spin_loaded.wav"
      "audio/525_spin_start_loaded.wav"
      "audio/525_step_1_1.wav"
      "audio/LICENSE-MAME-SAMPLES.txt"
      "font/P2000C font mini.png"
      "icons/drive-floppy-525.png"
      "icons/drive-hard-disk-sasi.png"
      "logo/logo-p2000c.svg"
      "images/cpm/chess.flp"
      "images/cpm/copower-boot.flp"
      "images/cpm/ipldump.flp"
      "images/cpm/p2edit.flp"
      "images/cpm/p2file.flp"
      "images/cpm/system.flp"
      "images/cpm/zork.flp"
      "images/hard-disks/blank.hda"
      "images/msdos/copower-msdos-2.11.flp"
      "images/msdos/applications/masm-3.01.flp"
      "images/msdos/applications/turbo-c-2.01-cli.flp"
      "tools/ipldump/IPLDUMP.BIN"
      "third_party/blink16_8086/LICENSE"
      "third_party/superzazu_z80/LICENSE"
      "LICENSE"
      "THIRD_PARTY.md"
  )
  target_link_libraries(
    p2000c PRIVATE p2000c_app_logic p2000c_core Qt6::Widgets)
  target_link_libraries(p2000c PRIVATE OpenAL::OpenAL)
  target_compile_definitions(p2000c PRIVATE
    P2000C_HAS_OPENAL=1
    P2000C_VERSION="${PROJECT_VERSION}"
    P2000C_SOURCE_MANUALS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manuals"
  )
  set_target_properties(p2000c PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "P2000C Emulator"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.p2000c.emulator"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
  if(APPLE)
    set_target_properties(p2000c PROPERTIES OUTPUT_NAME "P2000C Emulator")
    set(P2000C_PACKAGE_DATA_DIR
        "P2000C Emulator.app/Contents/Resources")
  else()
    set(P2000C_PACKAGE_DATA_DIR
        "${CMAKE_INSTALL_DATADIR}/p2000c-emulator")
  endif()

  install(TARGETS p2000c
    BUNDLE DESTINATION . COMPONENT application
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT application
  )
  install(DIRECTORY manuals/
    DESTINATION "${P2000C_PACKAGE_DATA_DIR}/manuals"
    COMPONENT application
    FILES_MATCHING PATTERN "*.pdf"
  )
  install(DIRECTORY images/
    DESTINATION "${P2000C_PACKAGE_DATA_DIR}/images"
    COMPONENT application
    FILES_MATCHING PATTERN "*.flp" PATTERN "*.hda"
  )
  install(FILES tools/ipldump/IPLDUMP.ASM tools/ipldump/IPLDUMP.BIN
    DESTINATION "${P2000C_PACKAGE_DATA_DIR}/firmware"
    COMPONENT application
  )
  install(FILES tools/p2file/P2FILE.ASM tools/p2file/README.md
    DESTINATION "${P2000C_PACKAGE_DATA_DIR}/utilities/p2file"
    COMPONENT application
  )
  install(FILES tools/p2edit/P2EDIT.ASM tools/p2edit/README.md
    DESTINATION "${P2000C_PACKAGE_DATA_DIR}/utilities/p2edit"
    COMPONENT application
  )
  install(FILES LICENSE THIRD_PARTY.md README.md
    DESTINATION "${P2000C_PACKAGE_DATA_DIR}/licenses"
    COMPONENT application
  )
  install(FILES third_party/blink16_8086/LICENSE
    DESTINATION "${P2000C_PACKAGE_DATA_DIR}/licenses/third-party"
    RENAME LICENSE-BLINK16.txt
    COMPONENT application
  )
  install(FILES third_party/superzazu_z80/LICENSE
    DESTINATION "${P2000C_PACKAGE_DATA_DIR}/licenses/third-party"
    RENAME LICENSE-SUPERZAZU-Z80.txt
    COMPONENT application
  )
  install(FILES audio/LICENSE-MAME-SAMPLES.txt
    DESTINATION "${P2000C_PACKAGE_DATA_DIR}/licenses/third-party"
    COMPONENT application
  )
  if(EXISTS "${QT6_INSTALL_PREFIX}/LICENSES")
    install(DIRECTORY "${QT6_INSTALL_PREFIX}/LICENSES/"
      DESTINATION "${P2000C_PACKAGE_DATA_DIR}/licenses/qt"
      COMPONENT application
    )
  endif()
  set(P2000C_OPENAL_LICENSE "")
  set(P2000C_VCPKG_OPENAL_LICENSE
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/openal-soft/copyright")
  if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET AND
     EXISTS "${P2000C_VCPKG_OPENAL_LICENSE}")
    set(P2000C_OPENAL_LICENSE
        "${P2000C_VCPKG_OPENAL_LICENSE}")
  elseif(EXISTS "/usr/share/doc/libopenal-dev/copyright")
    set(P2000C_OPENAL_LICENSE "/usr/share/doc/libopenal-dev/copyright")
  elseif(EXISTS
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openal-soft/COPYING")
    set(P2000C_OPENAL_LICENSE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openal-soft/COPYING")
  endif()
  if(P2000C_OPENAL_LICENSE)
    install(FILES "${P2000C_OPENAL_LICENSE}"
      DESTINATION "${P2000C_PACKAGE_DATA_DIR}/licenses/third-party"
      RENAME LICENSE-OPENAL-SOFT.txt
      COMPONENT application
    )
  endif()
  install(FILES logo/logo-p2000c.svg
    DESTINATION "${P2000C_PACKAGE_DATA_DIR}"
    COMPONENT application
  )
  if(UNIX AND NOT APPLE)
    set(P2000C_LINUX_OPENAL_INSTALL_CODE [=[
      file(INSTALL
        "$<TARGET_FILE:OpenAL::OpenAL>"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/@CMAKE_INSTALL_LIBDIR@"
        FOLLOW_SYMLINK_CHAIN)
    ]=])
    string(CONFIGURE "${P2000C_LINUX_OPENAL_INSTALL_CODE}"
           P2000C_LINUX_OPENAL_INSTALL_CODE @ONLY)
    install(CODE "${P2000C_LINUX_OPENAL_INSTALL_CODE}"
      COMPONENT application)
    install(FILES packaging/org.p2000c.emulator.desktop
      DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
      COMPONENT application
    )
    install(FILES logo/logo-p2000c.svg
      DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
      RENAME org.p2000c.emulator.svg
      COMPONENT application
    )
  endif()

  if(APPLE)
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
      set(P2000C_MACOS_RUNTIME_FILES
          "\"$<TARGET_LINKER_FILE:OpenAL::OpenAL>\"")
      if(TARGET fmt::fmt)
        list(APPEND P2000C_MACOS_RUNTIME_FILES
             "\"$<TARGET_LINKER_FILE:fmt::fmt>\"")
      endif()
      list(JOIN P2000C_MACOS_RUNTIME_FILES "\n        "
           P2000C_MACOS_RUNTIME_FILES)
      set(P2000C_MACOS_RUNTIME_INSTALL_CODE [=[
        file(INSTALL
          @P2000C_MACOS_RUNTIME_FILES@
          DESTINATION
            "${CMAKE_INSTALL_PREFIX}/P2000C Emulator.app/Contents/Frameworks"
          FOLLOW_SYMLINK_CHAIN)
      ]=])
      string(CONFIGURE "${P2000C_MACOS_RUNTIME_INSTALL_CODE}"
             P2000C_MACOS_RUNTIME_INSTALL_CODE @ONLY)
      install(CODE "${P2000C_MACOS_RUNTIME_INSTALL_CODE}"
        COMPONENT application)
    endif()
    # Qt 6.8.3 does not quote bundle names in its generated deploy script.
    install(CODE "
      include(\"${QT_DEPLOY_SUPPORT}\")
      qt_deploy_runtime_dependencies(
        EXECUTABLE \"$<TARGET_FILE_NAME:p2000c>.app\")
    " COMPONENT application)
  else()
    if(Qt6_VERSION VERSION_LESS 6.5)
      qt_generate_deploy_app_script(
        TARGET p2000c
        FILENAME_VARIABLE p2000c_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
      )
    else()
      qt_generate_deploy_app_script(
        TARGET p2000c
        OUTPUT_SCRIPT p2000c_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
      )
    endif()
    install(SCRIPT "${p2000c_deploy_script}" COMPONENT application)
  endif()
  if(WIN32)
    set(P2000C_WINDOWS_EXTRA_RUNTIME_DIRECTORY
        "$<TARGET_FILE_DIR:OpenAL::OpenAL>")
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
      set(P2000C_WINDOWS_EXTRA_RUNTIME_DIRECTORY
          "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    endif()
    set(P2000C_WINDOWS_RUNTIME_INSTALL_CODE [=[
      file(GLOB_RECURSE p2000c_installed_libraries
        "${CMAKE_INSTALL_PREFIX}/share/qt6/plugins/*.dll")
      file(GET_RUNTIME_DEPENDENCIES
        EXECUTABLES
          "$<TARGET_FILE:p2000c>"
        LIBRARIES ${p2000c_installed_libraries}
        RESOLVED_DEPENDENCIES_VAR p2000c_resolved_dependencies
        UNRESOLVED_DEPENDENCIES_VAR p2000c_unresolved_dependencies
        DIRECTORIES
          "$<TARGET_FILE_DIR:Qt6::Core>"
          "$<TARGET_FILE_DIR:OpenAL::OpenAL>"
          "@P2000C_WINDOWS_EXTRA_RUNTIME_DIRECTORY@"
        PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
        POST_EXCLUDE_REGEXES
          ".*[\\\\/][Ww]indows[\\\\/][Ss]ystem32[\\\\/].*")
      if(p2000c_unresolved_dependencies)
        list(JOIN p2000c_unresolved_dependencies ", " unresolved_list)
        message(FATAL_ERROR
          "Could not resolve Windows runtime dependencies: ${unresolved_list}")
      endif()
      foreach(dependency IN LISTS p2000c_resolved_dependencies)
        file(INSTALL
          DESTINATION "${CMAKE_INSTALL_PREFIX}/@CMAKE_INSTALL_BINDIR@"
          TYPE SHARED_LIBRARY
          FOLLOW_SYMLINK_CHAIN
          FILES "${dependency}")
      endforeach()
    ]=])
    string(CONFIGURE "${P2000C_WINDOWS_RUNTIME_INSTALL_CODE}"
           P2000C_WINDOWS_RUNTIME_INSTALL_CODE @ONLY)
    install(CODE "${P2000C_WINDOWS_RUNTIME_INSTALL_CODE}"
      COMPONENT application)
  endif()
endif()

if(BUILD_TESTING)
  add_test(
    NAME ipldump_format_test
    COMMAND ${CMAKE_COMMAND}
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tools/ipldump/IPLDUMP.ASM
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cpm_asm.cmake
  )

  add_test(
    NAME p2file_format_test
    COMMAND ${CMAKE_COMMAND}
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tools/p2file/P2FILE.ASM
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cpm_asm.cmake
  )

  add_test(
    NAME p2edit_format_test
    COMMAND ${CMAKE_COMMAND}
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tools/p2edit/P2EDIT.ASM
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cpm_asm.cmake
  )

  add_executable(raw_disk_image_test tests/raw_disk_image_test.cpp)
  target_link_libraries(raw_disk_image_test PRIVATE p2000c_core)
  add_test(
    NAME raw_disk_image_test
    COMMAND raw_disk_image_test
            ${CMAKE_CURRENT_SOURCE_DIR}/images/cpm/system.flp
            ${CMAKE_CURRENT_SOURCE_DIR}/images/hard-disks/blank.hda
            ${CMAKE_CURRENT_SOURCE_DIR}/images/msdos/copower-msdos-2.11.flp
  )

  add_executable(copower_board_test tests/copower_board_test.cpp)
  target_link_libraries(copower_board_test PRIVATE p2000c_core)
  add_test(NAME copower_board_test COMMAND copower_board_test)

  add_executable(
    fast_boot_controller_test tests/fast_boot_controller_test.cpp)
  target_link_libraries(
    fast_boot_controller_test PRIVATE p2000c_app_logic)
  add_test(NAME fast_boot_controller_test COMMAND fast_boot_controller_test)

  add_executable(p2000c_machine_test tests/p2000c_machine_test.cpp)
  target_link_libraries(p2000c_machine_test PRIVATE p2000c_core)
  add_test(
    NAME p2000c_machine_test
    COMMAND p2000c_machine_test
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/ipldump/IPLDUMP.BIN
            ${CMAKE_CURRENT_SOURCE_DIR}/images/cpm/system.flp
            ${CMAKE_CURRENT_SOURCE_DIR}/images/cpm/zork.flp
            ${CMAKE_CURRENT_SOURCE_DIR}/images/cpm/chess.flp
            ${CMAKE_CURRENT_SOURCE_DIR}/images/cpm/ipldump.flp
            ${CMAKE_CURRENT_SOURCE_DIR}/images/hard-disks/blank.hda
  )

  add_test(
    NAME p2000c_cli_test
    COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:p2000c_cli>
            -DIPL=${CMAKE_CURRENT_SOURCE_DIR}/tools/ipldump/IPLDUMP.BIN
            -DSYSTEM_DISK=${CMAKE_CURRENT_SOURCE_DIR}/images/cpm/system.flp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli.cmake
  )

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/media/floppies/COPOWERC.IMD" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/media/floppies/COPOWER.IMD")
    add_test(
      NAME copower_diagnostic_test
      COMMAND ${CMAKE_COMMAND}
              -DPROGRAM=$<TARGET_FILE:p2000c_cli>
              -DCONVERTER=$<TARGET_FILE:p2000c_imd_to_flp>
              -DIPL=${CMAKE_CURRENT_SOURCE_DIR}/tools/ipldump/IPLDUMP.BIN
              -DCOPOWER_IMD=${CMAKE_CURRENT_SOURCE_DIR}/media/floppies/COPOWERC.IMD
              -DCOPOWER_DOS_IMD=${CMAKE_CURRENT_SOURCE_DIR}/media/floppies/COPOWER.IMD
              -DCOPOWER_DOS_FLP=${CMAKE_CURRENT_SOURCE_DIR}/images/msdos/copower-msdos-2.11.flp
              -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_copower.cmake
    )
  endif()

  add_test(
    NAME copower_msboot_test
    COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:p2000c_cli>
            -DIPL=${CMAKE_CURRENT_SOURCE_DIR}/tools/ipldump/IPLDUMP.BIN
            -DCOPOWER_CPM_DISK=${CMAKE_CURRENT_SOURCE_DIR}/images/cpm/copower-boot.flp
            -DCOPOWER_DOS_DISK=${CMAKE_CURRENT_SOURCE_DIR}/images/msdos/copower-msdos-2.11.flp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_msboot.cmake
  )

  add_test(
    NAME copower_masm_test
    COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:p2000c_cli>
            -DIPL=${CMAKE_CURRENT_SOURCE_DIR}/tools/ipldump/IPLDUMP.BIN
            -DCOPOWER_CPM_DISK=${CMAKE_CURRENT_SOURCE_DIR}/images/cpm/copower-boot.flp
            -DMASM_DISK=${CMAKE_CURRENT_SOURCE_DIR}/images/msdos/applications/masm-3.01.flp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_masm.cmake
  )

  add_test(
    NAME copower_turbo_c_test
    COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:p2000c_cli>
            -DIPL=${CMAKE_CURRENT_SOURCE_DIR}/tools/ipldump/IPLDUMP.BIN
            -DCOPOWER_CPM_DISK=${CMAKE_CURRENT_SOURCE_DIR}/images/cpm/copower-boot.flp
            -DTURBO_C_DISK=${CMAKE_CURRENT_SOURCE_DIR}/images/msdos/applications/turbo-c-2.01-cli.flp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_turbo_c.cmake
  )

  add_test(
    NAME p2file_cli_test
    COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:p2000c_cli>
            -DIPL=${CMAKE_CURRENT_SOURCE_DIR}/tools/ipldump/IPLDUMP.BIN
            -DSYSTEM_DISK=${CMAKE_CURRENT_SOURCE_DIR}/images/cpm/system.flp
            -DP2FILE_DISK=${CMAKE_CURRENT_SOURCE_DIR}/images/cpm/p2file.flp
            -DHARD_DISK=${CMAKE_CURRENT_SOURCE_DIR}/images/hard-disks/blank.hda
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_p2file.cmake
  )

  add_test(
    NAME p2edit_cli_test
    COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:p2000c_cli>
            -DIPL=${CMAKE_CURRENT_SOURCE_DIR}/tools/ipldump/IPLDUMP.BIN
            -DSYSTEM_DISK=${CMAKE_CURRENT_SOURCE_DIR}/images/cpm/system.flp
            -DP2EDIT_DISK=${CMAKE_CURRENT_SOURCE_DIR}/images/cpm/p2edit.flp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_p2edit.cmake
  )

  find_program(P2000C_BASH_EXECUTABLE bash)
  if(P2000C_BASH_EXECUTABLE)
    add_test(
      NAME p2file_build_script_test
      COMMAND ${CMAKE_COMMAND} -E env
              "P2000C_CLI=$<TARGET_FILE:p2000c_cli>"
              ${P2000C_BASH_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/p2file/build.sh
              ${CMAKE_CURRENT_BINARY_DIR}/p2file-minimal-test.flp
    )
    add_test(
      NAME p2edit_build_script_test
      COMMAND ${CMAKE_COMMAND} -E env
              "P2000C_CLI=$<TARGET_FILE:p2000c_cli>"
              ${P2000C_BASH_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/p2edit/build.sh
              ${CMAKE_CURRENT_BINARY_DIR}/p2edit-minimal-test.flp
    )
  endif()

  if(P2000C_BUILD_APP)
    qt_add_executable(p2000c_app_test
      tests/main_window_test.cpp
      src/app/display_widget.cpp
      src/app/display_widget.h
      src/app/fast_boot_controller.h
      src/app/hardware_audio.cpp
      src/app/hardware_audio.h
      src/app/main_window.cpp
      src/app/main_window.h
      src/app/screen_color_dialog.cpp
      src/app/screen_color_dialog.h
    )
    qt_add_resources(p2000c_app_test "p2000c_test_assets"
      PREFIX "/"
      BASE "${CMAKE_CURRENT_SOURCE_DIR}"
      FILES
        "audio/525_seek_6ms.wav"
        "audio/525_spin_end.wav"
        "audio/525_spin_loaded.wav"
        "audio/525_spin_start_loaded.wav"
        "audio/525_step_1_1.wav"
        "audio/LICENSE-MAME-SAMPLES.txt"
        "font/P2000C font mini.png"
        "icons/drive-floppy-525.png"
        "icons/drive-hard-disk-sasi.png"
        "logo/logo-p2000c.svg"
        "images/cpm/chess.flp"
        "images/cpm/copower-boot.flp"
        "images/cpm/ipldump.flp"
        "images/cpm/p2edit.flp"
        "images/cpm/p2file.flp"
        "images/cpm/system.flp"
        "images/cpm/zork.flp"
        "images/hard-disks/blank.hda"
        "images/msdos/copower-msdos-2.11.flp"
        "images/msdos/applications/masm-3.01.flp"
        "images/msdos/applications/turbo-c-2.01-cli.flp"
        "tools/ipldump/IPLDUMP.BIN"
        "third_party/blink16_8086/LICENSE"
        "third_party/superzazu_z80/LICENSE"
        "LICENSE"
        "THIRD_PARTY.md"
    )
    target_link_libraries(
      p2000c_app_test PRIVATE p2000c_app_logic p2000c_core Qt6::Widgets)
    target_link_libraries(p2000c_app_test PRIVATE OpenAL::OpenAL)
    target_compile_definitions(p2000c_app_test PRIVATE
      P2000C_HAS_OPENAL=1
      P2000C_VERSION="${PROJECT_VERSION}"
      P2000C_SOURCE_MANUALS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manuals"
    )
    add_test(
      NAME p2000c_app_test
      COMMAND p2000c_app_test ${CMAKE_CURRENT_BINARY_DIR}/test_app_data
    )
    set_tests_properties(p2000c_app_test PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
  endif()
endif()

if(P2000C_BUILD_APP)
  set(CPACK_PACKAGE_NAME "P2000C Emulator")
  set(CPACK_PACKAGE_VENDOR "P2000C Emulator Project")
  set(CPACK_PACKAGE_CONTACT "https://github.com/ifilot/p2000c-emulator")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "An emulator for the Philips P2000C portable computer")
  set(CPACK_PACKAGE_HOMEPAGE_URL
      "https://github.com/ifilot/p2000c-emulator")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
  set(CPACK_COMPONENTS_ALL application)
  set(CPACK_GENERATOR "IFW")

  if(NOT P2000C_PACKAGE_ARCH)
    string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
           P2000C_PACKAGE_ARCH)
  endif()
  set(CPACK_PACKAGE_FILE_NAME
      "p2000c-emulator-${PROJECT_VERSION}-${P2000C_PACKAGE_ARCH}")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "P2000C Emulator")

  set(CPACK_IFW_PACKAGE_NAME "P2000C Emulator")
  set(CPACK_IFW_PACKAGE_TITLE "P2000C Emulator Setup")
  set(CPACK_IFW_PACKAGE_PUBLISHER "P2000C Emulator Project")
  set(CPACK_IFW_PRODUCT_URL "https://github.com/ifilot/p2000c-emulator")
  set(CPACK_IFW_PACKAGE_WIZARD_STYLE "Modern")
  set(CPACK_IFW_PACKAGE_WIZARD_SHOW_PAGE_LIST OFF)
  set(CPACK_IFW_PACKAGE_TITLE_COLOR "#913939")
  set(CPACK_IFW_PACKAGE_ALLOW_NON_ASCII_CHARACTERS ON)
  set(CPACK_IFW_PACKAGE_ALLOW_SPACE_IN_PATH ON)
  set(CPACK_IFW_PACKAGE_MAINTENANCE_TOOL_NAME
      "P2000C Emulator Maintenance")
  set(CPACK_IFW_ARCHIVE_COMPRESSION 9)
  if(WIN32)
    set(CPACK_IFW_TARGET_DIRECTORY
        "@ApplicationsDirX64@/P2000C Emulator")
    set(CPACK_PACKAGE_EXECUTABLES "p2000c" "P2000C Emulator")
    set(CPACK_CREATE_DESKTOP_LINKS "p2000c")
    set(CPACK_IFW_PACKAGE_RUN_PROGRAM "@TargetDir@/bin/p2000c.exe")
  elseif(APPLE)
    set(CPACK_IFW_TARGET_DIRECTORY
        "@ApplicationsDir@/P2000C Emulator")
    set(CPACK_IFW_PACKAGE_RUN_PROGRAM
        "@TargetDir@/P2000C Emulator.app/Contents/MacOS/P2000C Emulator")
  else()
    set(CPACK_IFW_TARGET_DIRECTORY "@HomeDir@/P2000C Emulator")
    set(CPACK_IFW_PACKAGE_RUN_PROGRAM "@TargetDir@/bin/p2000c")
  endif()
  set(CPACK_IFW_PACKAGE_RUN_PROGRAM_DESCRIPTION "Run P2000C Emulator")

  include(CPackComponent)
  cpack_add_component(application
    DISPLAY_NAME "P2000C Emulator"
    DESCRIPTION "Emulator, runtime libraries, disk templates, and manuals"
    REQUIRED
  )
  include(CPackIFW)
  cpack_ifw_configure_component(application
    FORCED_INSTALLATION
    NAME "org.p2000c.emulator"
    VERSION "${PROJECT_VERSION}"
    LICENSES "GNU GPL version 3" "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
  )
  include(CPack)
endif()
