foreach(_required CPKT_PATCH_WORKING_DIRECTORY CPKT_PATCH_SERIES)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

if(NOT IS_DIRECTORY "${CPKT_PATCH_WORKING_DIRECTORY}")
  message(FATAL_ERROR "patch working directory does not exist: ${CPKT_PATCH_WORKING_DIRECTORY}")
endif()
if(NOT EXISTS "${CPKT_PATCH_SERIES}")
  message(FATAL_ERROR "patch series does not exist: ${CPKT_PATCH_SERIES}")
endif()

find_program(CPKT_PATCH_EXECUTABLE NAMES gpatch patch)
if(NOT CPKT_PATCH_EXECUTABLE)
  message(FATAL_ERROR "GNU patch is required to apply ${CPKT_PATCH_SERIES}")
endif()
execute_process(
  COMMAND "${CPKT_PATCH_EXECUTABLE}" --version
  RESULT_VARIABLE _patch_version_result
  OUTPUT_VARIABLE _patch_version_output
  ERROR_QUIET)
if(NOT _patch_version_result EQUAL 0 OR NOT _patch_version_output MATCHES "GNU patch")
  message(FATAL_ERROR
    "GNU patch is required to apply ${CPKT_PATCH_SERIES}; found ${CPKT_PATCH_EXECUTABLE}. "
    "Install gpatch on macOS or patch on Linux.")
endif()

get_filename_component(_series_dir "${CPKT_PATCH_SERIES}" DIRECTORY)
file(READ "${CPKT_PATCH_SERIES}" _series_text)
string(REPLACE "\r\n" "\n" _series_text "${_series_text}")
string(REPLACE "\n" ";" _series_entries "${_series_text}")

get_filename_component(_series_name "${CPKT_PATCH_SERIES}" NAME)
set(_patch_state_dir "${CPKT_PATCH_WORKING_DIRECTORY}/.cpkt-patch-state")
set(_series_state_path "${_patch_state_dir}/${_series_name}.source-state")
set(_tracked_paths "")
foreach(_patch_name IN LISTS _series_entries)
  string(STRIP "${_patch_name}" _patch_name)
  if(_patch_name STREQUAL "" OR _patch_name MATCHES "^#")
    continue()
  endif()
  set(_patch_path "${_series_dir}/${_patch_name}")
  if(NOT EXISTS "${_patch_path}")
    message(FATAL_ERROR "patch listed in series does not exist: ${_patch_path}")
  endif()
  file(STRINGS "${_patch_path}" _added_paths REGEX "^\\+\\+\\+ b/")
  foreach(_added_path IN LISTS _added_paths)
    string(REGEX REPLACE "^\\+\\+\\+ b/" "" _added_path "${_added_path}")
    list(APPEND _tracked_paths "${_added_path}")
  endforeach()
endforeach()
list(REMOVE_DUPLICATES _tracked_paths)
list(SORT _tracked_paths)

function(cpkt_patch_source_state out_var)
  set(_state "")
  foreach(_tracked_path IN LISTS _tracked_paths)
    set(_source_path "${CPKT_PATCH_WORKING_DIRECTORY}/${_tracked_path}")
    if(EXISTS "${_source_path}")
      file(SHA256 "${_source_path}" _source_hash)
    else()
      set(_source_hash "MISSING")
    endif()
    string(APPEND _state "${_tracked_path}=${_source_hash}\n")
  endforeach()
  set(${out_var} "${_state}" PARENT_SCOPE)
endfunction()

if(EXISTS "${_series_state_path}")
  file(READ "${_series_state_path}" _recorded_source_state)
  cpkt_patch_source_state(_current_source_state)
  if(NOT _recorded_source_state STREQUAL _current_source_state)
    message(FATAL_ERROR
      "patched source changed after applying ${CPKT_PATCH_SERIES}; refresh the cached source tree")
  endif()
endif()

foreach(_patch_name IN LISTS _series_entries)
  string(STRIP "${_patch_name}" _patch_name)
  if(_patch_name STREQUAL "" OR _patch_name MATCHES "^#")
    continue()
  endif()

  set(_patch_path "${_series_dir}/${_patch_name}")
  if(NOT EXISTS "${_patch_path}")
    message(FATAL_ERROR "patch listed in series does not exist: ${_patch_path}")
  endif()

  set(_patch_state_path "${_patch_state_dir}/${_patch_name}.sha256")
  file(SHA256 "${_patch_path}" _patch_hash)
  if(EXISTS "${_patch_state_path}")
    file(READ "${_patch_state_path}" _applied_hash)
    string(STRIP "${_applied_hash}" _applied_hash)
    if(NOT _applied_hash STREQUAL _patch_hash)
      message(FATAL_ERROR
        "applied patch changed: ${_patch_path}; refresh the cached source tree")
    endif()
    message(STATUS "Skipping already-applied patch ${_patch_path}")
    continue()
  endif()

  execute_process(
    COMMAND "${CPKT_PATCH_EXECUTABLE}" --force --dry-run --reverse -p1 -i "${_patch_path}"
    WORKING_DIRECTORY "${CPKT_PATCH_WORKING_DIRECTORY}"
    RESULT_VARIABLE _patch_reverse_result
    OUTPUT_QUIET
    ERROR_QUIET
  )
  if(_patch_reverse_result EQUAL 0)
    message(STATUS "Skipping already-applied patch ${_patch_path}")
    file(MAKE_DIRECTORY "${_patch_state_dir}")
    file(WRITE "${_patch_state_path}" "${_patch_hash}\n")
    continue()
  endif()

  execute_process(
    COMMAND "${CPKT_PATCH_EXECUTABLE}" --force --dry-run -p1 -i "${_patch_path}"
    WORKING_DIRECTORY "${CPKT_PATCH_WORKING_DIRECTORY}"
    RESULT_VARIABLE _patch_dry_run_result
    OUTPUT_VARIABLE _patch_dry_run_output
    ERROR_VARIABLE _patch_dry_run_error
  )
  if(NOT _patch_dry_run_result EQUAL 0)
    message(FATAL_ERROR
      "failed to dry-run ${_patch_path} in ${CPKT_PATCH_WORKING_DIRECTORY}\n"
      "${_patch_dry_run_output}\n${_patch_dry_run_error}")
  endif()

  execute_process(
    COMMAND "${CPKT_PATCH_EXECUTABLE}" --force -p1 -i "${_patch_path}"
    WORKING_DIRECTORY "${CPKT_PATCH_WORKING_DIRECTORY}"
    RESULT_VARIABLE _patch_result
    OUTPUT_VARIABLE _patch_output
    ERROR_VARIABLE _patch_error
  )
  if(NOT _patch_result EQUAL 0)
    message(FATAL_ERROR
      "failed to apply ${_patch_path} in ${CPKT_PATCH_WORKING_DIRECTORY}\n"
      "${_patch_output}\n${_patch_error}")
  endif()
  file(MAKE_DIRECTORY "${_patch_state_dir}")
  file(WRITE "${_patch_state_path}" "${_patch_hash}\n")
endforeach()

cpkt_patch_source_state(_current_source_state)
file(MAKE_DIRECTORY "${_patch_state_dir}")
file(WRITE "${_series_state_path}" "${_current_source_state}")
