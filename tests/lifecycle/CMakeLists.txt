cmake_minimum_required(VERSION 3.21)
project(cpkt_lifecycle_tests NONE)
enable_testing()
get_filename_component(CPKT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_test(NAME host_bash COMMAND bash "${CPKT_ROOT}/tests/host_bash_test.sh" "${CPKT_ROOT}")
add_test(NAME github_actions_contracts COMMAND "${CMAKE_COMMAND}" "-DCPKT_REPO=${CPKT_ROOT}"
  -P "${CPKT_ROOT}/tests/github_actions_contract_test.cmake")
add_test(NAME lifecycle_dispatch
  COMMAND bash "${CPKT_ROOT}/tests/lifecycle_dispatch_test.sh" "${CPKT_ROOT}" "${CMAKE_BINARY_DIR}")
set_tests_properties(lifecycle_dispatch PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME dependency_inputs
  COMMAND "${CMAKE_COMMAND}" "-DCPKT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/../.."
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../dependency_inputs_test.cmake")
set_tests_properties(dependency_inputs PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME archive_reuse COMMAND "${CMAKE_COMMAND}" "-DCPKT_ROOT=${CPKT_ROOT}"
  "-DCPKT_SCRATCH=${CMAKE_BINARY_DIR}" -P "${CPKT_ROOT}/tests/archive_reuse_test.cmake")
set_tests_properties(archive_reuse PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
  add_test(NAME verification_scopes
    COMMAND "${CMAKE_COMMAND}" "-DCPKT_ROOT=${CPKT_ROOT}" "-DCPKT_SCRATCH=${CMAKE_BINARY_DIR}"
      -P "${CPKT_ROOT}/tests/verification_scopes_test.cmake")
  set_tests_properties(verification_scopes PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
endif()
add_test(NAME sdk_native_validation
  COMMAND "${CMAKE_COMMAND}" "-DCPKT_ROOT=${CPKT_ROOT}" "-DCPKT_SCRATCH=${CMAKE_BINARY_DIR}"
    -P "${CPKT_ROOT}/tests/sdk_native_validation_test.cmake")
set_tests_properties(sdk_native_validation PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME release_identity
  COMMAND bash "${CPKT_ROOT}/tests/release_identity_test.sh" "${CPKT_ROOT}" "${CMAKE_BINARY_DIR}")
set_tests_properties(release_identity PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
