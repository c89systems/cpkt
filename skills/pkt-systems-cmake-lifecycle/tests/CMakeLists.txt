cmake_minimum_required(VERSION 3.21)
project(lifecycle_skill_tests NONE)
enable_testing()
if(NOT SKILL_WORKSPACE_ROOT OR NOT EXISTS "${SKILL_WORKSPACE_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR "Pass SKILL_WORKSPACE_ROOT for repository-local fixture scratch")
endif()
find_program(HOST_BASH bash REQUIRED)
if(IS_SYMLINK "${SKILL_WORKSPACE_ROOT}/build" OR
    IS_SYMLINK "${SKILL_WORKSPACE_ROOT}/build/skill-resolver-fixtures")
  message(FATAL_ERROR "Fixture scratch must not be redirected by symlinks")
endif()
file(READ "${SKILL_WORKSPACE_ROOT}/.gitignore" workspace_ignore)
if(NOT workspace_ignore MATCHES "(^|[\r\n])/build/([\r\n]|$)")
  message(FATAL_ERROR "Fixture scratch requires ignored /build/")
endif()
add_test(NAME native_test_reuse
  COMMAND "${HOST_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/test-native-test-reuse.sh"
    "${SKILL_WORKSPACE_ROOT}")
add_test(NAME release_contract
  COMMAND "${HOST_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/test-release-tag-contract.sh"
    "${SKILL_WORKSPACE_ROOT}")
add_test(NAME source_root_context
  COMMAND "${HOST_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/test-source-root-context.sh"
    "${SKILL_WORKSPACE_ROOT}")
set_tests_properties(native_test_reuse release_contract source_root_context PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  set(resolver_scratch "${SKILL_WORKSPACE_ROOT}/build/skill-resolver-fixtures")
  file(MAKE_DIRECTORY "${resolver_scratch}")
  add_test(NAME toolchain_resolvers
    COMMAND "${HOST_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/test-cpkt-toolchain-resolvers.sh")
  set_tests_properties(toolchain_resolvers PROPERTIES
    LABELS resolver TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT "TMPDIR=${resolver_scratch}")
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    add_test(NAME aflpp_resolver
      COMMAND "${HOST_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/test-cpkt-aflpp-resolver.sh")
    set_tests_properties(aflpp_resolver PROPERTIES
      LABELS resolver TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT "TMPDIR=${resolver_scratch}")
  endif()
endif()
