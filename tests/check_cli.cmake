if(NOT DEFINED PROGRAM OR NOT DEFINED IPL OR NOT DEFINED SYSTEM_DISK)
  message(FATAL_ERROR "PROGRAM, IPL, and SYSTEM_DISK are required")
endif()

file(SHA256 "${SYSTEM_DISK}" system_hash_before)
execute_process(
  COMMAND "${PROGRAM}"
          --ipl "${IPL}"
          --floppy-a "${SYSTEM_DISK}"
          --fast-storage
          --wait-for "A>"
          --send "SAVE 1 CLITEST.COM\\r"
          --run 20000000
          --send "DIR\\r"
          --wait-for "CLITEST"
          --dump-memory "0x0000:4"
          --output json
  RESULT_VARIABLE cli_result
  OUTPUT_VARIABLE cli_output
  ERROR_VARIABLE cli_error
)
file(SHA256 "${SYSTEM_DISK}" system_hash_after)

if(NOT cli_result EQUAL 0)
  message(FATAL_ERROR
          "Headless CLI scenario failed (${cli_result}):\n${cli_error}\n${cli_output}")
endif()
if(NOT system_hash_before STREQUAL system_hash_after)
  message(FATAL_ERROR "The CLI modified source media without --write-through")
endif()
foreach(expected IN ITEMS
        "\"status\": \"ok\""
        "CLITEST"
        "\"address\": \"0x0000\""
        "\"program_counter\""
        "\"screen\"")
  string(FIND "${cli_output}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
            "CLI JSON did not contain ${expected}:\n${cli_output}")
  endif()
endforeach()

execute_process(
  COMMAND "${PROGRAM}"
          --ipl "${IPL}"
          --wait-for "TEXT THAT CANNOT APPEAR"
          --wait-cycles 1000
          --output json
  RESULT_VARIABLE timeout_result
  OUTPUT_VARIABLE timeout_output
  ERROR_VARIABLE timeout_error
)
if(NOT timeout_result EQUAL 3)
  message(FATAL_ERROR
          "CLI timeout returned ${timeout_result}, expected 3:\n${timeout_error}")
endif()
string(FIND "${timeout_output}" "\"status\": \"timeout\"" timeout_found)
if(timeout_found EQUAL -1)
  message(FATAL_ERROR "CLI timeout did not produce structured output")
endif()
