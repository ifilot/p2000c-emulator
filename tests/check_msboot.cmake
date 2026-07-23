if(NOT DEFINED PROGRAM OR NOT DEFINED IPL OR
   NOT DEFINED COPOWER_CPM_DISK OR NOT DEFINED COPOWER_DOS_DISK)
  message(FATAL_ERROR
          "PROGRAM, IPL, COPOWER_CPM_DISK, and COPOWER_DOS_DISK are required")
endif()

file(SHA256 "${COPOWER_CPM_DISK}" cpm_hash_before)
file(SHA256 "${COPOWER_DOS_DISK}" dos_hash_before)
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
          --swap-floppy-a "${COPOWER_DOS_DISK}"
          --send "\\r"
          --wait-for "Enter new date:"
          --run 100000
          --send "\\r"
          --wait-for "Enter new time:"
          --run 100000
          --send "\\r"
          --wait-for "A>"
          --send "DIR\\r"
          --wait-for "COMMAND  COM"
          --wait-cycles 300000000
          --output json
  RESULT_VARIABLE msboot_result
  OUTPUT_VARIABLE msboot_output
  ERROR_VARIABLE msboot_error
)
file(SHA256 "${COPOWER_CPM_DISK}" cpm_hash_after)
file(SHA256 "${COPOWER_DOS_DISK}" dos_hash_after)

if(NOT msboot_result EQUAL 0)
  message(FATAL_ERROR
          "CoPower MSBOOT scenario failed (${msboot_result}):\n"
          "${msboot_error}\n${msboot_output}")
endif()
if(NOT cpm_hash_before STREQUAL cpm_hash_after OR
   NOT dos_hash_before STREQUAL dos_hash_after)
  message(FATAL_ERROR "MSBOOT modified bundled source media")
endif()
foreach(expected IN ITEMS
        "\"status\": \"ok\""
        "\"enabled\": true"
        "\"faulted\": false"
        "P2000C / 8088"
        "Microsoft MS-DOS version 2.11"
        "SYSTEM-V1-0"
        "COMMAND")
  string(FIND "${msboot_output}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR
            "MSBOOT output did not contain ${expected}:\n${msboot_output}")
  endif()
endforeach()
