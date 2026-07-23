foreach(required IN ITEMS PROGRAM IPL COPOWER_CPM_DISK MASM_DISK)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

file(SHA256 "${COPOWER_CPM_DISK}" cpm_hash_before)
file(SHA256 "${MASM_DISK}" masm_hash_before)
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
          --swap-floppy-a "${MASM_DISK}"
          --send "\\r"
          --wait-for "Enter new date:"
          --run 100000
          --send "\\r"
          --wait-for "Enter new time:"
          --run 100000
          --send "\\r"
          --wait-for "A>"
          --send "MASM HELLO;\\r"
          --wait-for "0       0"
          --run 1000000
          --send "LINK HELLO;\\r"
          --wait-for "Microsoft 8086 Object Linker"
          --run 10000000
          --send "HELLO\\r"
          --wait-for "Hello from MASM on the P2000C!"
          --wait-cycles 500000000
          --output text
  RESULT_VARIABLE masm_result
  OUTPUT_VARIABLE masm_output
  ERROR_VARIABLE masm_error
)
file(SHA256 "${COPOWER_CPM_DISK}" cpm_hash_after)
file(SHA256 "${MASM_DISK}" masm_hash_after)

if(NOT masm_result EQUAL 0)
  message(FATAL_ERROR
    "MASM example failed through CoPower (${masm_result}):\n"
    "${masm_error}\n${masm_output}")
endif()
if(NOT cpm_hash_before STREQUAL cpm_hash_after OR
   NOT masm_hash_before STREQUAL masm_hash_after)
  message(FATAL_ERROR "MASM example modified immutable bundled source media")
endif()
string(FIND "${masm_output}" "Hello from MASM on the P2000C!"
       hello_position)
if(hello_position EQUAL -1)
  message(FATAL_ERROR
    "MASM example output was not observed:\n${masm_output}")
endif()
