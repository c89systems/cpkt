if(NOT DEFINED CPKT_SOURCE_DIR OR CPKT_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "CPKT_SOURCE_DIR is required")
endif()
if(NOT DEFINED CPKT_TEST_BINARY_DIR OR CPKT_TEST_BINARY_DIR STREQUAL "")
  message(FATAL_ERROR "CPKT_TEST_BINARY_DIR is required")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${CPKT_SOURCE_DIR}/tests/curl_system_truststore_fixture"
    -B "${CPKT_TEST_BINARY_DIR}"
    "-DCPKT_SOURCE_DIR=${CPKT_SOURCE_DIR}"
  RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "curl system trust-store fixture failed")
endif()
