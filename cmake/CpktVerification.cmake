# CMake output stamps reuse complete CTest scopes. No per-case result database.
function(cpkt_add_verification_scopes)
  get_property(_tests DIRECTORY PROPERTY TESTS)
  get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  set(_runtime_binaries "")
  set(_libraries "")
  foreach(_target IN LISTS _targets)
    get_target_property(_type "${_target}" TYPE)
    if(_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY)$")
      list(APPEND _runtime_binaries "$<TARGET_FILE:${_target}>")
      if(NOT _type STREQUAL "EXECUTABLE")
        list(APPEND _libraries "$<TARGET_FILE:${_target}>")
      endif()
    endif()
  endforeach()
  foreach(_scope host runtime)
    set(_names "")
    set(_inputs "")
    set(_definition "${CPKT_TARGET_ID}\n${CMAKE_CROSSCOMPILING_EMULATOR}\n")
    foreach(_test IN LISTS _tests)
      get_test_property("${_test}" LABELS _labels)
      if((_scope STREQUAL "host" AND "host-lifecycle" IN_LIST _labels) OR
          (_scope STREQUAL "runtime" AND NOT "host-lifecycle" IN_LIST _labels))
        list(APPEND _names "${_test}")
        get_property(_command GLOBAL PROPERTY "CPKT_TEST_COMMAND_${_test}")
        get_property(_declared GLOBAL PROPERTY "CPKT_TEST_INPUTS_${_test}")
        list(APPEND _inputs ${_declared})
        string(APPEND _definition "${_test}: ${_command}\ninputs=${_declared}\n")
        foreach(_property TIMEOUT ENVIRONMENT WORKING_DIRECTORY WILL_FAIL
            PASS_REGULAR_EXPRESSION FAIL_REGULAR_EXPRESSION FIXTURES_REQUIRED)
          get_test_property("${_test}" "${_property}" _value)
          string(APPEND _definition "${_property}=${_value}\n")
        endforeach()
        foreach(_property DISABLED SKIP_RETURN_CODE SKIP_REGULAR_EXPRESSION)
          get_property(_set TEST "${_test}" PROPERTY "${_property}" SET)
          get_test_property("${_test}" "${_property}" _value)
          if(_set AND (_value OR NOT _property STREQUAL "DISABLED"))
            message(FATAL_ERROR "Required CTest case may not skip: ${_test}: ${_property}")
          endif()
        endforeach()
      endif()
    endforeach()
    if(NOT _names)
      continue()
    endif()
    set(_base "${CMAKE_BINARY_DIR}/verification/${_scope}")
    cpkt_validate_mutation_paths("${_base}.inputs" "${_base}.passed")
    file(GENERATE OUTPUT "${_base}.inputs" CONTENT "${_definition}")
    if(_scope STREQUAL "host")
      set(_selection -L host-lifecycle)
    else()
      set(_selection -LE host-lifecycle)
      list(APPEND _inputs ${_runtime_binaries})
    endif()
    list(REMOVE_DUPLICATES _inputs)
    add_custom_command(OUTPUT "${_base}.passed"
      COMMAND "${CMAKE_COMMAND}" -E rm -f "${_base}.passed"
      COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
        ${_selection} --no-tests=error --stop-on-failure --output-on-failure
      COMMAND "${CMAKE_COMMAND}" -E touch "${_base}.passed"
      DEPENDS "${_base}.inputs" ${_inputs}
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" VERBATIM)
    add_custom_target(cpkt_verify_${_scope} DEPENDS "${_base}.passed")
  endforeach()
  set(_memory_names "")
  set(_memory_inputs ${_libraries})
  set(_memory_definition "${CPKT_TARGET_ID}\n${CMAKE_CROSSCOMPILING_EMULATOR}\n")
  foreach(_test IN LISTS _tests)
    get_test_property("${_test}" LABELS _labels)
    if("memcheck" IN_LIST _labels)
      string(APPEND _memory_names "${_test}\n")
      get_property(_command GLOBAL PROPERTY "CPKT_TEST_COMMAND_${_test}")
      get_property(_declared GLOBAL PROPERTY "CPKT_TEST_INPUTS_${_test}")
      list(APPEND _memory_inputs ${_declared})
      list(GET _command 0 _executable)
      if(TARGET "${_executable}")
        list(APPEND _memory_inputs "$<TARGET_FILE:${_executable}>")
      endif()
      string(APPEND _memory_definition "${_test}: ${_command}\ninputs=${_declared}\n")
      foreach(_property TIMEOUT ENVIRONMENT WORKING_DIRECTORY WILL_FAIL
          PASS_REGULAR_EXPRESSION FAIL_REGULAR_EXPRESSION FIXTURES_REQUIRED)
        get_test_property("${_test}" "${_property}" _value)
        string(APPEND _memory_definition "${_property}=${_value}\n")
      endforeach()
    endif()
  endforeach()
  if(_memory_names AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
      CPKT_TARGET_ID STREQUAL "x86_64-linux-gnu")
    set(_base "${CMAKE_BINARY_DIR}/verification/memcheck")
    file(GENERATE OUTPUT "${_base}.names" CONTENT "${_memory_names}")
    file(GENERATE OUTPUT "${_base}.inputs" CONTENT "${_memory_definition}")
    string(JSON _suppression ERROR_VARIABLE _missing GET "${CPKT_INVENTORY}"
      groups "${CPKT_GROUP}" hardening memcheck_suppression)
    set(_extra_inputs "")
    if(NOT _missing)
      set(_suppression "${CMAKE_SOURCE_DIR}/${_suppression}")
      list(APPEND _extra_inputs "${_suppression}")
    else()
      set(_suppression "")
    endif()
    add_custom_command(OUTPUT "${_base}.passed"
      COMMAND "${CMAKE_COMMAND}" -E rm -f "${_base}.passed"
      COMMAND "${CMAKE_CTEST_COMMAND}" -S "${CMAKE_SOURCE_DIR}/cmake/memcheck.cmake"
        "-DCPKT_SOURCE=${CMAKE_SOURCE_DIR}" "-DCPKT_BINARY=${CMAKE_BINARY_DIR}"
        "-DCPKT_SUPPRESSIONS=${_suppression}"
      COMMAND "${CMAKE_COMMAND}" -E touch "${_base}.passed"
      DEPENDS "${_base}.inputs" "${_base}.names"
        "${CMAKE_SOURCE_DIR}/cmake/memcheck.cmake" ${_memory_inputs} ${_extra_inputs}
      VERBATIM)
    add_custom_target(cpkt_verify_memcheck DEPENDS "${_base}.passed")
  endif()
endfunction()

function(cpkt_register_fuzz_checks)
  if(NOT TARGET cpkt_lua_runtime_fuzz)
    return()
  endif()
  file(GLOB seeds CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/fuzz/seeds/lua/*")
  foreach(mode smoke standard long)
    set(stamp "${CMAKE_BINARY_DIR}/verification/fuzz-${mode}.passed")
    add_custom_command(OUTPUT "${stamp}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/verification"
      COMMAND "${CMAKE_COMMAND}" -E rm -f "${stamp}"
      COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/run-afl-fuzz.sh" "${mode}"
        "$<TARGET_FILE:cpkt_lua_runtime_fuzz>" "${CMAKE_SOURCE_DIR}/fuzz/seeds/lua"
      COMMAND "${CMAKE_COMMAND}" -E touch "${stamp}"
      DEPENDS cpkt_lua_runtime_fuzz ${seeds} "${CMAKE_SOURCE_DIR}/scripts/run-afl-fuzz.sh" VERBATIM)
    add_custom_target(cpkt_verify_fuzz_${mode} DEPENDS "${stamp}")
  endforeach()
endfunction()
cmake_language(DEFER CALL cpkt_register_fuzz_checks)
