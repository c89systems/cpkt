include_guard(GLOBAL)
function(cpkt_literal_argument output value)
  set(_equals "")
  string(LENGTH "${value}" _value_length)
  # Include the closer in the search: a terminal ] or ]= can overlap it.
  string(FIND "${value}]${_equals}]" "]${_equals}]" _closing)
  while(NOT _closing EQUAL _value_length)
    string(APPEND _equals "=")
    string(FIND "${value}]${_equals}]" "]${_equals}]" _closing)
  endwhile()
  # CMake ignores the first newline after a bracket opener. Supply that newline
  # ourselves so a value beginning with a newline retains it too.
  set(${output} "[${_equals}[\n${value}]${_equals}]" PARENT_SCOPE)
endfunction()
