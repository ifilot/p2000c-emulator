if(NOT DEFINED PROGRAM OR NOT DEFINED IPL OR NOT DEFINED SYSTEM_DISK OR
   NOT DEFINED P2UTIL_DISK OR NOT DEFINED HARD_DISK)
  message(FATAL_ERROR
    "PROGRAM, IPL, SYSTEM_DISK, P2UTIL_DISK, and HARD_DISK are required")
endif()

file(SHA256 "${SYSTEM_DISK}" system_hash_before)
file(SHA256 "${P2UTIL_DISK}" p2util_hash_before)
file(SHA256 "${HARD_DISK}" hard_hash_before)

set(system_work "${CMAKE_CURRENT_BINARY_DIR}/p2file-system.flp")
set(p2file_work "${CMAKE_CURRENT_BINARY_DIR}/p2file-p2util.flp")
set(hard_work "${CMAKE_CURRENT_BINARY_DIR}/p2file-hard.hda")
file(COPY_FILE "${SYSTEM_DISK}" "${system_work}" ONLY_IF_DIFFERENT)
file(COPY_FILE "${P2UTIL_DISK}" "${p2file_work}" ONLY_IF_DIFFERENT)
file(COPY_FILE "${HARD_DISK}" "${hard_work}" ONLY_IF_DIFFERENT)

set(scroll_down_actions)
set(scroll_up_actions)
set(populate_commands "")
foreach(step RANGE 1 17)
  string(APPEND populate_commands "SAVE 1 T${step}.TST\\r")
endforeach()
foreach(step RANGE 1 30)
  list(APPEND scroll_down_actions --send "S" --run 5000000)
  list(APPEND scroll_up_actions --send "W" --run 5000000)
endforeach()

# Build P2FILE with the CP/M assembler and loader, then exercise every mutating
# operation. Fixed runs after screen redraws keep scripted keystrokes clear of
# the P2000C terminal's serial output bursts.
execute_process(
  COMMAND "${PROGRAM}"
          --ipl "${IPL}"
          --floppy-a "${system_work}"
          --floppy-b "${p2file_work}"
          --hard-disk-0 "${hard_work}"
          --write-through
          --fast-storage
          --wait-cycles 1000000000
          --wait-for "A>"
          --send "B:\\rASM P2FILE\\r"
          --wait-for "END OF ASSEMBLY"
          --send "LOAD P2FILE\\r"
          --wait-for "FIRST ADDRESS"
          --send "${populate_commands}"
          --run 100000000
          --send "P2FILE\\r"
          --wait-for "DRIVE A:"
          --run 20000000
          --wait-for "27 FILES"
          --wait-for "T12     .TST     1K"
          --send "\\x18"
          --run 5000000
          --wait-for "DRIVE A:   16 FILES   2/ 16"
          --send "\\x05"
          --run 5000000
          --wait-for "DRIVE A:   16 FILES   1/ 16"
          --send "\\t"
          --run 5000000
          --wait-for "ASM     .COM     8K"
          ${scroll_down_actions}
          --wait-for "27/ 27 ^"
          --wait-for "T17     .TST     1K"
          ${scroll_up_actions}
          --wait-for "1/ 27  v"
          --send "\\t"
          --run 5000000
          --send "V"
          --wait-for "SELECT DRIVE"
          --wait-for "A:  B:  C:  D:  E:  F:"
          --wait-for "PRESS A-F OR ESC TO CANCEL"
          --wait-for "INSERT DISK BEFORE SELECTING"
          --run 5000000
          --send "B"
          --run 20000000
          --send "C"
          --wait-for "Choose two different drives before copying."
          --send "S"
          --run 5000000
          --wait-for "TAB PANEL  W/S MOVE"
          --send "V"
          --wait-for "SELECT DRIVE"
          --wait-for "PRESS A-F OR ESC TO CANCEL"
          --run 5000000
          --send "C"
          --run 20000000
          --send "\\t"
          --run 20000000
          --send " "
          --run 10000000
          --send "S"
          --run 10000000
          --send " "
          --run 10000000
          --send "C"
          --wait-for "COPYING FILES"
          --wait-for "FILE   1 OF   2"
          --wait-for "RECORDS"
          --wait-for "Copy complete."
          --wait-for "TAB PANEL  W/S MOVE"
          --run 10000000
          --send "C"
          --wait-for "Overwrite LOAD.COM? (Y/N)"
          --send "Y"
          --wait-for "COPYING FILES"
          --wait-for "Copy complete."
          --wait-for "TAB PANEL  W/S MOVE"
          --run 10000000
          --send "\\t"
          --run 10000000
          --send "R"
          --wait-for "New name"
          --send "RENAMED.COM\\r"
          --wait-for "Rename complete."
          --run 10000000
          --send " "
          --run 5000000
          --send "S"
          --run 5000000
          --send " "
          --run 5000000
          --send "D"
          --wait-for "Delete selected file(s)? (Y/N)"
          --send "Y"
          --wait-for "Delete complete."
          --run 10000000
          --send "Q"
          --run 20000000
          --send "DIR C:\\r"
          --wait-for "NO FILE"
          --run 5000000
          --output text
  RESULT_VARIABLE p2file_result
  OUTPUT_VARIABLE p2file_output
  ERROR_VARIABLE p2file_error
)

if(NOT p2file_result EQUAL 0)
  message(FATAL_ERROR
    "P2FILE CLI scenario failed (${p2file_result}):\n"
    "${p2file_error}\n${p2file_output}")
endif()

foreach(expected IN ITEMS "P2FILE finished." "B>DIR C:" "NO FILE")
  string(FIND "${p2file_output}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "P2FILE final screen did not contain ${expected}:\n${p2file_output}")
  endif()
endforeach()

# The test uses explicit build-tree media copies; repository media stay pristine.
file(SHA256 "${SYSTEM_DISK}" system_hash_after)
file(SHA256 "${P2UTIL_DISK}" p2util_hash_after)
file(SHA256 "${HARD_DISK}" hard_hash_after)
if(NOT system_hash_before STREQUAL system_hash_after OR
   NOT p2util_hash_before STREQUAL p2util_hash_after OR
   NOT hard_hash_before STREQUAL hard_hash_after)
  message(FATAL_ERROR "P2FILE CLI test modified source P2UTIL media")
endif()
