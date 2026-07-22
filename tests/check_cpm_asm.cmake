if(NOT DEFINED INPUT)
  message(FATAL_ERROR "INPUT must name the CP/M assembly source")
endif()

file(READ "${INPUT}" source_hex HEX)
string(TOLOWER "${source_hex}" source_hex)
get_filename_component(source_name "${INPUT}" NAME)

if(NOT source_hex MATCHES "1a$")
  message(FATAL_ERROR "${source_name} does not end with the CP/M 1AH marker")
endif()
string(REGEX REPLACE "1a$" "" source_body_hex "${source_hex}")
if(source_body_hex MATCHES "1a")
  message(FATAL_ERROR "${source_name} contains an early CP/M 1AH marker")
endif()

string(REGEX REPLACE "0d0a" "" without_crlf "${source_hex}")
if(without_crlf MATCHES "0a" OR without_crlf MATCHES "0d")
  message(FATAL_ERROR "${source_name} contains a non-CP/M line ending")
endif()

if(source_hex MATCHES "^efbbbf")
  message(FATAL_ERROR "${source_name} must not contain a Unicode byte-order mark")
endif()

# Keep every source line within columns 1-79. This is deliberately stricter
# than the P2000C terminal's 80-column width, avoiding wrap at the right edge.
file(STRINGS "${INPUT}" source_lines)
set(line_number 0)
foreach(source_line IN LISTS source_lines)
  math(EXPR line_number "${line_number} + 1")
  string(LENGTH "${source_line}" line_length)
  if(line_length GREATER 79)
    message(FATAL_ERROR
      "${source_name} line ${line_number} uses ${line_length} columns")
  endif()
endforeach()

# Keep source lines below the 80-column terminal width. Hexadecimal splitting
# avoids treating the assembly comment character as a CMake list separator.
string(REPLACE "0d0a" ";" source_lines_hex "${source_hex}")
set(line_number 0)
foreach(line_hex IN LISTS source_lines_hex)
  math(EXPR line_number "${line_number} + 1")
  string(LENGTH "${line_hex}" hex_length)
  math(EXPR line_length "${hex_length} / 2")
  if(line_length GREATER 79)
    message(FATAL_ERROR
            "${source_name} line ${line_number} is ${line_length} columns wide")
  endif()
endforeach()
