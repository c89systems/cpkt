foreach(_required CPKT_SOURCE_DIR CPKT_TEST_BINARY_DIR CPKT_TEST_CC CPKT_TEST_AR CPKT_TEST_RANLIB)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _suffix)
set(_work_dir "${CPKT_TEST_BINARY_DIR}/archive-member-${_suffix}")
file(MAKE_DIRECTORY "${_work_dir}")
file(WRITE "${_work_dir}/member.c" "int cpkt_archive_fixture(void) { return 7; }\n")
execute_process(
  COMMAND "${CPKT_TEST_CC}" -c "${_work_dir}/member.c" -o "${_work_dir}/member.o"
  RESULT_VARIABLE _result ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "fixture compile failed: ${_error}")
endif()

execute_process(
  COMMAND "${CPKT_TEST_AR}" rcs "${_work_dir}/libinner.a" "${_work_dir}/member.o"
  RESULT_VARIABLE _result ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "inner archive creation failed: ${_error}")
endif()
execute_process(
  COMMAND "${CPKT_TEST_AR}" rcs "${_work_dir}/libouter.a"
    "${_work_dir}/member.o" "${_work_dir}/libinner.a"
  RESULT_VARIABLE _result ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "outer archive creation failed: ${_error}")
endif()
execute_process(
  COMMAND "${CPKT_TEST_AR}" t "${_work_dir}/libouter.a"
  RESULT_VARIABLE _result OUTPUT_VARIABLE _initial_members ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "outer archive listing failed: ${_error}")
endif()
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin"
    AND NOT _initial_members MATCHES "(^|\n)libinner\\.a(\n|$)")
  message(FATAL_ERROR "fixture did not contain the nested archive: ${_initial_members}")
endif()

foreach(_attempt RANGE 1 2)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DCPKT_STATIC_ARCHIVE=${_work_dir}/libouter.a"
      -DCPKT_STATIC_ARCHIVE_MEMBER=libinner.a
      "-DCPKT_STATIC_ARCHIVER=${CPKT_TEST_AR}"
      "-DCPKT_STATIC_RANLIB=${CPKT_TEST_RANLIB}"
      -P "${CPKT_SOURCE_DIR}/cmake/remove_static_archive_member.cmake"
    RESULT_VARIABLE _result ERROR_VARIABLE _error)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "archive cleanup attempt ${_attempt} failed: ${_error}")
  endif()
  execute_process(
    COMMAND "${CPKT_TEST_AR}" t "${_work_dir}/libouter.a"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _members ERROR_VARIABLE _error)
  if(NOT _result EQUAL 0 OR _members MATCHES "(^|\n)libinner\\.a(\n|$)"
      OR NOT _members MATCHES "(^|\n)member\\.o(\n|$)")
    message(FATAL_ERROR "invalid outer archive after attempt ${_attempt}: ${_members} ${_error}")
  endif()
endforeach()

file(WRITE "${_work_dir}/consumer.c"
  "int cpkt_archive_fixture(void);\nint main(void) { return cpkt_archive_fixture() != 7; }\n")
execute_process(
  COMMAND "${CPKT_TEST_CC}" "${_work_dir}/consumer.c" "${_work_dir}/libouter.a"
    ${CPKT_TEST_RUNTIME_LINK_OPTIONS}
    -o "${_work_dir}/consumer"
  RESULT_VARIABLE _result ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "cleaned archive consumer failed to link: ${_error}")
endif()
if(CPKT_TEST_RUNTIME_LOADER)
  execute_process(
    COMMAND bash "${CPKT_SOURCE_DIR}/tests/local_executable_runtime_policy_test.sh"
      "${CPKT_TEST_RUNTIME_LOADER}" "${_work_dir}/consumer"
    RESULT_VARIABLE _result ERROR_VARIABLE _error)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "cleaned archive consumer runtime policy failed: ${_error}")
  endif()
endif()
execute_process(COMMAND ${CPKT_TEST_EXECUTABLE_PREFIX} "${_work_dir}/consumer"
  RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "cleaned archive consumer failed: ${_result}")
endif()

# An index-only archive must fail before staging; member listings can include
# a Darwin symbol-table entry even when all actual object files are absent.
execute_process(
  COMMAND "${CPKT_TEST_AR}" d "${_work_dir}/libouter.a" member.o
  RESULT_VARIABLE _result ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "empty archive fixture failed: ${_error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DCPKT_STATIC_ARCHIVE=${_work_dir}/libouter.a"
    -DCPKT_STATIC_ARCHIVE_MEMBER=libinner.a
    "-DCPKT_STATIC_ARCHIVER=${CPKT_TEST_AR}"
    "-DCPKT_STATIC_RANLIB=${CPKT_TEST_RANLIB}"
    -P "${CPKT_SOURCE_DIR}/cmake/remove_static_archive_member.cmake"
  RESULT_VARIABLE _result ERROR_VARIABLE _error)
if(_result EQUAL 0 OR NOT _error MATCHES "no object members")
  message(FATAL_ERROR "empty archive was not rejected: ${_error}")
endif()

file(REMOVE_RECURSE "${_work_dir}")
