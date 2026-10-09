# Small assertion helper for native CMake integration tests.
function(cpkt_test_command)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Command failed (${result}): ${ARGV}\n${output}${error}")
  endif()
  set(CPKT_TEST_OUTPUT "${output}" PARENT_SCOPE)
endfunction()
function(cpkt_test_failure diagnostic)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "${diagnostic}")
    message(FATAL_ERROR "Expected rejection /${diagnostic}/ (${result}): ${ARGN}\n${output}${error}")
  endif()
endfunction()
