if(NOT DEFINED CPKT_SOURCE_DIR OR NOT DEFINED CPKT_PATCH_TEST_ROOT)
  message(FATAL_ERROR "CPKT_SOURCE_DIR and CPKT_PATCH_TEST_ROOT are required")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _suffix)
set(_work_dir "${CPKT_PATCH_TEST_ROOT}/patch-series-${_suffix}")
file(MAKE_DIRECTORY "${_work_dir}")
file(WRITE "${_work_dir}/message.txt" "before\n")
file(WRITE "${_work_dir}/change.patch"
  "--- a/message.txt\n+++ b/message.txt\n@@ -1 +1 @@\n-before\n+after\n")
file(WRITE "${_work_dir}/second.patch"
  "--- a/message.txt\n+++ b/message.txt\n@@ -1 +1 @@\n-after\n+final\n")
file(WRITE "${_work_dir}/series" "change.patch\nsecond.patch\n")

foreach(_attempt RANGE 1 2)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DCPKT_PATCH_WORKING_DIRECTORY=${_work_dir}"
      "-DCPKT_PATCH_SERIES=${_work_dir}/series"
      -P "${CPKT_SOURCE_DIR}/cmake/apply_patch_series.cmake"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "patch attempt ${_attempt} failed:\n${_output}\n${_error}")
  endif()
  file(READ "${_work_dir}/message.txt" _actual)
  if(NOT _actual STREQUAL "final\n")
    message(FATAL_ERROR "patch attempt ${_attempt} produced ${_actual}; expected final")
  endif()
endforeach()

file(WRITE "${_work_dir}/message.txt" "unexpected\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DCPKT_PATCH_WORKING_DIRECTORY=${_work_dir}"
    "-DCPKT_PATCH_SERIES=${_work_dir}/series"
    -P "${CPKT_SOURCE_DIR}/cmake/apply_patch_series.cmake"
  RESULT_VARIABLE _invalid_result
  OUTPUT_QUIET
  ERROR_QUIET)
if(_invalid_result EQUAL 0)
  message(FATAL_ERROR "patch helper accepted a source file matching neither direction")
endif()
file(READ "${_work_dir}/message.txt" _invalid_actual)
if(NOT _invalid_actual STREQUAL "unexpected\n")
  message(FATAL_ERROR "failed patch changed the input source")
endif()

file(REMOVE_RECURSE "${_work_dir}")
