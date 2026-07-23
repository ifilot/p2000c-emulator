if(NOT DEFINED PROGRAM OR NOT DEFINED CONVERTER OR NOT DEFINED IPL OR
   NOT DEFINED COPOWER_IMD OR NOT DEFINED COPOWER_DOS_IMD OR
   NOT DEFINED COPOWER_DOS_FLP)
  message(FATAL_ERROR
          "PROGRAM, CONVERTER, IPL, and both CoPower images are required")
endif()

set(copower_flp "${CMAKE_CURRENT_BINARY_DIR}/copower-test.flp")
set(copower_dos_flp "${CMAKE_CURRENT_BINARY_DIR}/copower-dos-test.flp")
execute_process(
  COMMAND "${CONVERTER}" "${COPOWER_IMD}" "${copower_flp}"
  RESULT_VARIABLE convert_result
  ERROR_VARIABLE convert_error
)
if(NOT convert_result EQUAL 0)
  message(FATAL_ERROR "Could not convert CoPower IMD: ${convert_error}")
endif()
execute_process(
  COMMAND "${CONVERTER}" "${COPOWER_DOS_IMD}" "${copower_dos_flp}"
  RESULT_VARIABLE dos_convert_result
  ERROR_VARIABLE dos_convert_error
)
if(NOT dos_convert_result EQUAL 0)
  message(FATAL_ERROR
          "Could not convert CoPower DOS IMD: ${dos_convert_error}")
endif()
file(SHA256 "${copower_dos_flp}" converted_dos_hash)
file(SHA256 "${COPOWER_DOS_FLP}" bundled_dos_hash)
if(NOT converted_dos_hash STREQUAL bundled_dos_hash)
  message(FATAL_ERROR
          "Bundled CoPower DOS FLP does not match the original IMD conversion")
endif()

foreach(test_spec IN ITEMS
        "S;Short memory test"
        "I;Interrupt test"
        "R;Refresh & Lock Test")
  list(GET test_spec 0 menu_key)
  list(GET test_spec 1 heading)
  execute_process(
    COMMAND "${PROGRAM}"
            --ipl "${IPL}"
            --floppy-a "${copower_flp}"
            --fast-storage
            --copower
            --wait-for "A>"
            --send "TEST88\\r"
            --wait-for "MATH COPROCESSOR"
            --run 100000
            --send "N"
            --wait-for "MENU:"
            --run 100000
            --send "${menu_key}"
            --wait-for "PASS 00001"
            --wait-cycles 300000000
            --output json
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_output
    ERROR_VARIABLE test_error
  )
  if(NOT test_result EQUAL 0)
    message(FATAL_ERROR
            "CoPower ${heading} failed (${test_result}):\n"
            "${test_error}\n${test_output}")
  endif()
  foreach(expected IN ITEMS
          "\"status\": \"ok\""
          "\"enabled\": true"
          "\"faulted\": false"
          "${heading}"
          "PASS 00001")
    string(FIND "${test_output}" "${expected}" found)
    if(found EQUAL -1)
      message(FATAL_ERROR
              "CoPower ${heading} output did not contain ${expected}:\n"
              "${test_output}")
    endif()
  endforeach()
endforeach()

file(REMOVE "${copower_flp}" "${copower_dos_flp}")
