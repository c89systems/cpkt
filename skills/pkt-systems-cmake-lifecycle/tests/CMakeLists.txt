cmake_minimum_required(VERSION 3.21)
project(lifecycle_skill_tests NONE)
enable_testing()
if(NOT SKILL_WORKSPACE_ROOT OR NOT EXISTS "${SKILL_WORKSPACE_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR "Pass SKILL_WORKSPACE_ROOT for repository-local fixture scratch")
endif()
find_program(HOST_BASH bash REQUIRED)
add_test(NAME native_test_reuse
  COMMAND "${HOST_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/test-native-test-reuse.sh"
    "${SKILL_WORKSPACE_ROOT}")
add_test(NAME release_policy_wording
  COMMAND "${HOST_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/test-release-tag-contract.sh")
set_tests_properties(native_test_reuse release_policy_wording PROPERTIES TIMEOUT 60)
