if(NOT DEFINED CPKT_SOURCE_DIR OR "${CPKT_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CPKT_SOURCE_DIR is required")
endif()
if(NOT DEFINED CPKT_TEST_BINARY_DIR OR "${CPKT_TEST_BINARY_DIR}" STREQUAL "")
  message(FATAL_ERROR "CPKT_TEST_BINARY_DIR is required")
endif()

set(fixture_source_dir
  "${CPKT_SOURCE_DIR}/tests/external_project_toolchain_env_fixture")
set(fixture_build_dir "${CPKT_TEST_BINARY_DIR}")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${fixture_source_dir}"
    -B "${fixture_build_dir}"
    "-DCPKT_SOURCE_DIR=${CPKT_SOURCE_DIR}"
  RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "External-project toolchain environment fixture failed")
endif()
