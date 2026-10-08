cmake_minimum_required(VERSION 3.21)
execute_process(COMMAND ${CPKT_RUNNER} "${CPKT_EXECUTABLE}" failure
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 1 OR NOT "${output}${error}" MATCHES "intentional failure 37"
    OR NOT "${output}${error}" MATCHES "cmocka_downstream_behavior.c:")
  message(FATAL_ERROR "cmocka failure contract changed (${result}):\n${output}${error}")
endif()
