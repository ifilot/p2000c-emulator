if(NOT DEFINED INSTALL_ROOT OR NOT IS_DIRECTORY "${INSTALL_ROOT}")
  message(FATAL_ERROR "INSTALL_ROOT must name an installed package tree")
endif()
if(NOT DEFINED PLATFORM)
  message(FATAL_ERROR "PLATFORM must be linux, windows, or macos")
endif()

function(require_file path)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Required package file is missing: ${path}")
  endif()
endfunction()

function(require_glob description)
  file(GLOB matches ${ARGN})
  if(NOT matches)
    message(FATAL_ERROR "Required packaged ${description} was not found")
  endif()
endfunction()

if(PLATFORM STREQUAL "macos")
  set(app_root "${INSTALL_ROOT}/P2000C Emulator.app")
  set(data_root "${app_root}/Contents/Resources")
  require_file("${app_root}/Contents/MacOS/P2000C Emulator")
  if(REQUIRE_RUNTIME)
    require_glob("Qt Core framework"
      "${app_root}/Contents/Frameworks/QtCore.framework/*/QtCore"
      "${app_root}/Contents/Frameworks/QtCore.framework/QtCore")
    require_glob("Cocoa platform plugin"
      "${app_root}/Contents/PlugIns/platforms/*qcocoa*")
    require_glob("OpenAL runtime"
      "${app_root}/Contents/Frameworks/*openal*.dylib"
      "${app_root}/Contents/Frameworks/*OpenAL*.dylib")
  endif()
elseif(PLATFORM STREQUAL "windows")
  set(data_root "${INSTALL_ROOT}/share/p2000c-emulator")
  require_file("${INSTALL_ROOT}/bin/p2000c.exe")
  if(REQUIRE_RUNTIME)
    require_glob("Qt Core runtime" "${INSTALL_ROOT}/bin/Qt6Core.dll")
    require_glob("Windows platform plugin"
      "${INSTALL_ROOT}/bin/platforms/qwindows.dll")
    require_glob("OpenAL runtime"
      "${INSTALL_ROOT}/bin/OpenAL32.dll"
      "${INSTALL_ROOT}/bin/openal32.dll")
  endif()
elseif(PLATFORM STREQUAL "linux")
  set(data_root "${INSTALL_ROOT}/share/p2000c-emulator")
  require_file("${INSTALL_ROOT}/bin/p2000c")
  require_file("${INSTALL_ROOT}/share/applications/org.p2000c.emulator.desktop")
  require_file(
    "${INSTALL_ROOT}/share/icons/hicolor/scalable/apps/org.p2000c.emulator.svg")
  if(REQUIRE_RUNTIME)
    require_glob("Qt Core runtime" "${INSTALL_ROOT}/lib/libQt6Core.so*")
    require_glob("XCB platform plugin"
      "${INSTALL_ROOT}/plugins/platforms/libqxcb.so")
    require_glob("OpenAL runtime" "${INSTALL_ROOT}/lib/libopenal.so*")
  endif()
else()
  message(FATAL_ERROR "Unsupported PLATFORM value: ${PLATFORM}")
endif()

foreach(manual IN ITEMS
    P2000C-SystemRefServiceManual.pdf
    P2519CPM_UserGuide.pdf
    P2519_CPM_Reference.pdf)
  set(path "${data_root}/manuals/${manual}")
  require_file("${path}")
  file(SIZE "${path}" size)
  if(size LESS 100000)
    message(FATAL_ERROR "Packaged manual is unexpectedly small: ${path}")
  endif()
endforeach()

foreach(floppy IN ITEMS chess.flp ipldump.flp system.flp zork.flp)
  set(path "${data_root}/images/${floppy}")
  require_file("${path}")
  file(SIZE "${path}" size)
  if(NOT size EQUAL 655360)
    message(FATAL_ERROR "Packaged floppy has invalid size: ${path}")
  endif()
endforeach()

set(hard_disk "${data_root}/images/blank.hda")
require_file("${hard_disk}")
file(SIZE "${hard_disk}" hard_disk_size)
if(NOT hard_disk_size EQUAL 10485760)
  message(FATAL_ERROR "Packaged hard disk has invalid size: ${hard_disk}")
endif()

require_file("${data_root}/firmware/IPLDUMP.ASM")
require_file("${data_root}/firmware/IPLDUMP.BIN")
require_file("${data_root}/licenses/LICENSE")
require_file("${data_root}/licenses/THIRD_PARTY.md")
require_file("${data_root}/licenses/README.md")
require_file("${data_root}/licenses/third-party/LICENSE")
require_file("${data_root}/licenses/third-party/LICENSE-MAME-SAMPLES.txt")
require_file("${data_root}/licenses/third-party/LICENSE-FONT-AWESOME.txt")
if(REQUIRE_RUNTIME)
  require_file("${data_root}/licenses/third-party/LICENSE-OPENAL-SOFT.txt")
endif()
require_file("${data_root}/logo-p2000c.svg")

message(STATUS "Verified complete ${PLATFORM} installation at ${INSTALL_ROOT}")
