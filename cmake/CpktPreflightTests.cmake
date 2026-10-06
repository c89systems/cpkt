# Shared native registration for standalone preflight and the SDK graph.
function(cpkt_register_preflight_tests repo_root)
  set(CPKT_REPO_ROOT "${repo_root}")
  set(_wanted OFF)
  if(DEFINED CPKT_PREFLIGHT_CASES)
    if("sdk_runtime_loader" IN_LIST CPKT_PREFLIGHT_CASES)
      set(_wanted ON)
    endif()
  elseif((CPKT_BUILD_TESTS) AND (CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    set(_wanted ON)
  endif()
  if(_wanted)
    cpkt_group_add_test(NAME sdk_runtime_loader
            COMMAND "${Python3_EXECUTABLE}" "${CPKT_REPO_ROOT}/tests/sdk_runtime_loader_test.py"
              --scratch "${CMAKE_BINARY_DIR}" --target "${CPKT_TARGET_ID}"
              --compiler "${CMAKE_C_COMPILER}" --readelf "${CMAKE_READELF}"
              --sysroot "${CMAKE_SYSROOT}")
  endif()
  set(_wanted OFF)
  if(DEFINED CPKT_PREFLIGHT_CASES)
    if("krb5_trace_prototypes" IN_LIST CPKT_PREFLIGHT_CASES)
      set(_wanted ON)
    endif()
  elseif((CPKT_BUILD_TESTS))
    set(_wanted ON)
  endif()
  if(_wanted)
    cpkt_group_add_test(NAME krb5_trace_prototypes
          COMMAND "${Python3_EXECUTABLE}" "${CPKT_REPO_ROOT}/tests/krb5_trace_prototypes_test.py"
            "${CPKT_REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_C_COMPILER}")
  endif()
  set(_wanted OFF)
  if(DEFINED CPKT_PREFLIGHT_CASES)
    if("patch_series_portability" IN_LIST CPKT_PREFLIGHT_CASES)
      set(_wanted ON)
    endif()
  elseif((CPKT_BUILD_TESTS) AND (CPKT_CAN_RUN_TARGET_EXECUTABLES))
    set(_wanted ON)
  endif()
  if(_wanted)
    cpkt_group_add_test(
              NAME patch_series_portability
              COMMAND "${CMAKE_COMMAND}"
                "-DCPKT_SOURCE_DIR=${CPKT_REPO_ROOT}"
                "-DCPKT_PATCH_TEST_ROOT=${CMAKE_BINARY_DIR}"
                -P "${CPKT_REPO_ROOT}/tests/patch_series_portability_test.cmake")
  endif()
  set(_wanted OFF)
  if(DEFINED CPKT_PREFLIGHT_CASES)
    if("static_archive_member_cleanup" IN_LIST CPKT_PREFLIGHT_CASES)
      set(_wanted ON)
    endif()
  elseif((CPKT_BUILD_TESTS) AND (CPKT_CAN_RUN_TARGET_EXECUTABLES))
    set(_wanted ON)
  endif()
  if(_wanted)
    cpkt_group_add_test(
              NAME static_archive_member_cleanup
              COMMAND "${CMAKE_COMMAND}"
                "-DCPKT_SOURCE_DIR=${CPKT_REPO_ROOT}"
                "-DCPKT_TEST_BINARY_DIR=${CMAKE_BINARY_DIR}"
                "-DCPKT_TEST_CC=${CMAKE_C_COMPILER}"
                "-DCPKT_TEST_AR=${CMAKE_AR}"
                "-DCPKT_TEST_RANLIB=${CMAKE_RANLIB}"
                "-DCPKT_TEST_EXECUTABLE_PREFIX=${CPKT_TEST_EXECUTABLE_PREFIX}"
                "-DCPKT_TEST_RUNTIME_LINK_OPTIONS=${CPKT_LOCAL_RUNTIME_LINK_OPTIONS}"
                "-DCPKT_TEST_RUNTIME_LOADER=${CPKT_RUNTIME_LOADER}"
                -P "${CPKT_REPO_ROOT}/tests/static_archive_member_cleanup_test.cmake")
  endif()
  set(_wanted OFF)
  if(DEFINED CPKT_PREFLIGHT_CASES)
    if("source_archive_verify_failure" IN_LIST CPKT_PREFLIGHT_CASES)
      set(_wanted ON)
    endif()
  elseif((CPKT_BUILD_TESTS) AND (CPKT_CAN_RUN_TARGET_EXECUTABLES) AND (CPKT_BUILD_TESTS) AND (CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    set(_wanted ON)
  endif()
  if(_wanted)
    cpkt_group_add_test(NAME source_archive_verify_failure
                  COMMAND bash "${CPKT_REPO_ROOT}/tests/source_archive_verify_failure_test.sh")
  endif()
  set(_wanted OFF)
  if(DEFINED CPKT_PREFLIGHT_CASES)
    if("dependency_install_order_Ninja" IN_LIST CPKT_PREFLIGHT_CASES)
      set(_wanted ON)
    endif()
  elseif((CPKT_BUILD_TESTS) AND (CPKT_CAN_RUN_TARGET_EXECUTABLES) AND (CPKT_BUILD_TESTS) AND (CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    set(_wanted ON)
  endif()
  if(_wanted)
    cpkt_group_add_test(NAME dependency_install_order_Ninja
                      COMMAND "${Python3_EXECUTABLE}" "${CPKT_REPO_ROOT}/tests/dependency_install_order_test.py"
                        "${CPKT_REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}" "Ninja")
  endif()
  set(_wanted OFF)
  if(DEFINED CPKT_PREFLIGHT_CASES)
    if("dependency_install_order_Makefiles" IN_LIST CPKT_PREFLIGHT_CASES)
      set(_wanted ON)
    endif()
  elseif((CPKT_BUILD_TESTS) AND (CPKT_CAN_RUN_TARGET_EXECUTABLES) AND (CPKT_BUILD_TESTS) AND (CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    set(_wanted ON)
  endif()
  if(_wanted)
    cpkt_group_add_test(NAME dependency_install_order_Makefiles
                      COMMAND "${Python3_EXECUTABLE}" "${CPKT_REPO_ROOT}/tests/dependency_install_order_test.py"
                        "${CPKT_REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}" "Unix Makefiles")
  endif()
  set(_wanted OFF)
  if(DEFINED CPKT_PREFLIGHT_CASES)
    if("zlib_feature_namespace" IN_LIST CPKT_PREFLIGHT_CASES)
      set(_wanted ON)
    endif()
  elseif((CPKT_BUILD_TESTS) AND (CPKT_CAN_RUN_TARGET_EXECUTABLES) AND (CPKT_BUILD_TESTS) AND (CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    set(_wanted ON)
  endif()
  if(_wanted)
    cpkt_group_add_test(NAME zlib_feature_namespace
                  COMMAND "${Python3_EXECUTABLE}" "${CPKT_REPO_ROOT}/tests/zlib_feature_namespace_test.py"
                    "${CPKT_REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_C_COMPILER}")
  endif()
endfunction()
