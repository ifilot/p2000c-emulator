if(NOT DEFINED PROGRAM OR NOT DEFINED IPL OR NOT DEFINED SYSTEM_DISK OR
   NOT DEFINED P2EDIT_DISK)
  message(FATAL_ERROR
    "PROGRAM, IPL, SYSTEM_DISK, and P2EDIT_DISK are required")
endif()

file(SHA256 "${SYSTEM_DISK}" system_hash_before)
file(SHA256 "${P2EDIT_DISK}" p2edit_hash_before)

set(system_work "${CMAKE_CURRENT_BINARY_DIR}/p2edit-system.flp")
set(p2edit_work "${CMAKE_CURRENT_BINARY_DIR}/p2edit-tools.flp")
file(COPY_FILE "${SYSTEM_DISK}" "${system_work}" ONLY_IF_DIFFERENT)
file(COPY_FILE "${P2EDIT_DISK}" "${p2edit_work}" ONLY_IF_DIFFERENT)

set(move_right_actions)
foreach(step RANGE 1 5)
  list(APPEND move_right_actions --send "\\x06" --run 1000000)
endforeach()

set(search_actions)
foreach(key IN ITEMS S E C O N D)
  list(APPEND search_actions --send "${key}" --run 500000)
endforeach()

set(save_name_actions)
foreach(key IN ITEMS B ":" N "." T)
  list(APPEND save_name_actions --send "${key}" --run 1000000)
endforeach()

# Keep scripted keystrokes slightly separated to model the physical keyboard.
# The short interval also guards the incremental redraw path: restoring a full
# screen repaint after every ordinary key makes this scenario lose input.
execute_process(
  COMMAND "${PROGRAM}"
          --ipl "${IPL}"
          --floppy-a "${system_work}"
          --floppy-b "${p2edit_work}"
          --write-through
          --fast-storage
          --wait-cycles 1000000000
          --wait-for "A>"
          --send "B:\\rASM P2EDIT\\r"
          --wait-for "END OF ASSEMBLY"
          --run 5000000
          --send "LOAD P2EDIT\\r"
          --wait-for "FIRST ADDRESS"
          --run 5000000
          --send "P2EDIT B:TEST.TXT\\r"
          --wait-for "File read"
          --run 5000000
          --wait-for "Ln 1/2"
          --wait-for "HELLO FROM P2EDIT"
          --wait-for "SECOND LINE"
          --send "\\x05"
          --run 1000000
          --wait-for "Col 7"
          --send "\\x05"
          --run 1000000
          --wait-for "Col 12"
          --send "\\x02"
          --run 1000000
          --wait-for "Col 7"
          --send "\\x01"
          --run 1000000
          --wait-for "Col 1"
          ${move_right_actions}
          --send "!"
          --run 1000000
          --wait-for "HELLO! FROM P2EDIT"
          # Word navigation must also work while the cursor-centred gap is open.
          --send "\\x05"
          --run 1000000
          --wait-for "Col 8"
          --send "\\x02"
          --run 1000000
          --wait-for "Col 1"
          ${move_right_actions}
          --send "\\x06"
          --run 1000000
          # Move the live gap left, delete from its suffix, and insert again.
          --send "\\x15"
          --run 1000000
          --send "\\x7f"
          --run 1000000
          --wait-for "HELLO FROM P2EDIT"
          --send "!"
          --run 1000000
          --wait-for "HELLO! FROM P2EDIT"
          --send "\\x17"
          --wait-for "Find:"
          --run 2000000
          ${search_actions}
          --send "\\r"
          --wait-for "Found"
          --run 3000000
          --wait-for "Ln 2/2"
          --send "\\x0b"
          --wait-for "Line cut"
          --run 3000000
          --wait-for "HELLO! FROM P2EDIT"
          --send "\\x10"
          --wait-for "Clipboard pasted"
          --run 5000000
          --wait-for "SECOND LINE"
          --send "Z"
          --run 1000000
          --send "\\x08"
          --run 1000000
          --wait-for "SECOND LINE"
          --send "\\x0f"
          --wait-for "Wrote B:TEST.TXT"
          --run 5000000
          --send "\\x18"
          --wait-for "B>"
          --send "TYPE TEST.TXT\\r"
          --wait-for "HELLO! FROM P2EDIT"
          --wait-for "SECOND LINE"
          --run 3000000
          --send "P2EDIT B:TEST.TXT\\r"
          --wait-for "File read"
          --run 3000000
          --wait-for "Ln 1/2"
          --send "\\x04"
          --run 1000000
          --send "\\x7f"
          --run 3000000
          --wait-for "Ln 1/1"
          --wait-for "HELLO! FROM P2EDITSECOND LINE"
          --send "\\x18"
          --wait-for "Save changes before exit? (Y/N/C)"
          --run 2000000
          --send "C"
          --wait-for "HELLO! FROM P2EDITSECOND LINE"
          --run 3000000
          --send "\\x18"
          --wait-for "Save changes before exit? (Y/N/C)"
          --run 2000000
          --send "N"
          --wait-for "B>"
          --send "P2EDIT\\r"
          --wait-for "New buffer"
          --run 3000000
          --wait-for "Ln 1/1"
          --send "X"
          --run 1000000
          --send "\\r"
          --run 3000000
          --wait-for "Ln 2/2"
          --send "\\x08"
          --run 3000000
          --wait-for "Ln 1/1"
          --send "\\x0f"
          --wait-for "Write file:"
          --run 2000000
          ${save_name_actions}
          --send "\\r"
          --wait-for "Wrote B:N.T"
          --run 3000000
          --send "\\x18"
          --wait-for "B>"
          --send "TYPE N.T\\r"
          --wait-for "X"
          --run 2000000
          --send "TYPE TEST.TXT\\r"
          --wait-for "HELLO! FROM P2EDIT"
          --wait-for "SECOND LINE"
          --output text
  RESULT_VARIABLE p2edit_result
  OUTPUT_VARIABLE p2edit_output
  ERROR_VARIABLE p2edit_error
)

if(NOT p2edit_result EQUAL 0)
  message(FATAL_ERROR
    "P2EDIT CLI scenario failed (${p2edit_result}):\n"
    "${p2edit_error}\n${p2edit_output}")
endif()

foreach(expected IN ITEMS "P2EDIT finished." "B>TYPE TEST.TXT"
                          "HELLO! FROM P2EDIT" "SECOND LINE")
  string(FIND "${p2edit_output}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "P2EDIT final output did not contain ${expected}:\n${p2edit_output}")
  endif()
endforeach()

file(SHA256 "${SYSTEM_DISK}" system_hash_after)
file(SHA256 "${P2EDIT_DISK}" p2edit_hash_after)
if(NOT system_hash_before STREQUAL system_hash_after OR
   NOT p2edit_hash_before STREQUAL p2edit_hash_after)
  message(FATAL_ERROR "P2EDIT CLI test modified source media")
endif()
