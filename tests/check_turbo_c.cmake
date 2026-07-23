foreach(required IN ITEMS PROGRAM IPL COPOWER_CPM_DISK TURBO_C_DISK)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

file(SHA256 "${COPOWER_CPM_DISK}" cpm_hash_before)
file(SHA256 "${TURBO_C_DISK}" turbo_c_hash_before)
execute_process(
  COMMAND "${PROGRAM}"
          --ipl "${IPL}"
          --floppy-a "${COPOWER_CPM_DISK}"
          --fast-storage
          --copower
          --wait-for "A>"
          --send "MSBOOT\\r"
          --wait-for "Insert MS-DOS Disk"
          --run 100000
          --swap-floppy-a "${TURBO_C_DISK}"
          --send "\\r"
          --wait-for "Enter new date:"
          --run 100000
          --send "\\r"
          --wait-for "Enter new time:"
          --run 100000
          --send "\\r"
          --wait-for "A>"
          --send "TCC HELLO.C\\r"
          --wait-for "Turbo C  Version 2.01"
          --wait-for "Turbo Link  Version 2.0"
          --wait-for "Available memory"
          --send "HELLO\\r"
          --wait-for "Hello from Turbo C 2.01 on the P2000C!"
          --send "TCC PRIMES.C\\r"
          --run 100000000
          --send "PRIMES\\r"
          --wait-for "There are 25 primes from 2 through 100."
          --send "TCC FILEIO.C\\r"
          --run 100000000
          --send "FILEIO\\r"
          --wait-for "Read back: Turbo C file I/O works."
          --wait-cycles 1000000000
          --output text
  RESULT_VARIABLE turbo_c_result
  OUTPUT_VARIABLE turbo_c_output
  ERROR_VARIABLE turbo_c_error
)
file(SHA256 "${COPOWER_CPM_DISK}" cpm_hash_after)
file(SHA256 "${TURBO_C_DISK}" turbo_c_hash_after)

if(NOT turbo_c_result EQUAL 0)
  message(FATAL_ERROR
    "Turbo C examples failed through CoPower (${turbo_c_result}):\n"
    "${turbo_c_error}\n${turbo_c_output}")
endif()
if(NOT cpm_hash_before STREQUAL cpm_hash_after OR
   NOT turbo_c_hash_before STREQUAL turbo_c_hash_after)
  message(FATAL_ERROR
    "Turbo C test modified immutable bundled source media")
endif()
foreach(expected IN ITEMS
    "Turbo C  Version 2.01"
    "Turbo Link  Version 2.0"
    "Hello from Turbo C 2.01 on the P2000C!"
    "There are 25 primes from 2 through 100."
    "Read back: Turbo C file I/O works.")
  string(FIND "${turbo_c_output}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
      "Turbo C output did not contain ${expected}:\n${turbo_c_output}")
  endif()
endforeach()
