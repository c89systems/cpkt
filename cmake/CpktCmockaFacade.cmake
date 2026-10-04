set(CPKT_CMOCKA_FACADE_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/cmocka/include")
add_custom_command(OUTPUT "${CPKT_CMOCKA_FACADE_INCLUDE_DIR}/cpkt/cmocka.h"
    "${CPKT_CMOCKA_FACADE_INCLUDE_DIR}/cpkt/cmocka_types.h"
    "${CPKT_CMOCKA_FACADE_INCLUDE_DIR}/cpkt/cmocka_bridge.inc"
    "${CPKT_CMOCKA_FACADE_INCLUDE_DIR}/cpkt/cmocka_bridge_exports.txt"
  COMMAND "${CPKT_OPERATION_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/generate_cmocka_c89.py"
    --header "${CPKT_EXTERNAL_ROOT}/cmocka/install/include/cmocka.h"
    --output "${CPKT_CMOCKA_FACADE_INCLUDE_DIR}/cpkt/cmocka.h"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/generate_cmocka_c89.py"
    "${CPKT_EXTERNAL_ROOT}/cmocka/install/include/cmocka.h"
  VERBATIM)
cpkt_group_add_custom_target(cpkt_cmocka_header
  DEPENDS "${CPKT_CMOCKA_FACADE_INCLUDE_DIR}/cpkt/cmocka.h"
    "${CPKT_CMOCKA_FACADE_INCLUDE_DIR}/cpkt/cmocka_types.h"
    "${CPKT_CMOCKA_FACADE_INCLUDE_DIR}/cpkt/cmocka_bridge.inc"
    "${CPKT_CMOCKA_FACADE_INCLUDE_DIR}/cpkt/cmocka_bridge_exports.txt")
foreach(_variant static shared)
  if(_variant STREQUAL "static")
    set(_type STATIC)
  else()
    set(_type SHARED)
  endif()
  cpkt_group_add_library(cpkt_cmocka_${_variant} ${_type} src/cmocka_c89.c)
  add_dependencies(cpkt_cmocka_${_variant} cpkt_cmocka_header)
  target_compile_options(cpkt_cmocka_${_variant} PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(cpkt_cmocka_${_variant})
  target_include_directories(cpkt_cmocka_${_variant} PUBLIC "${CPKT_CMOCKA_FACADE_INCLUDE_DIR}")
  target_link_libraries(cpkt_cmocka_${_variant} PUBLIC cpkt::cmocka_${_variant} m)
  set_target_properties(cpkt_cmocka_${_variant} PROPERTIES OUTPUT_NAME cpkt_cmocka
    POSITION_INDEPENDENT_CODE ON)
  if(_variant STREQUAL "shared")
    cpkt_apply_auth_export_catalog(cpkt_cmocka_shared cpkt_cmocka)
    set_target_properties(cpkt_cmocka_shared PROPERTIES SOVERSION 0 VERSION "${PROJECT_VERSION}")
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
      set_target_properties(cpkt_cmocka_shared PROPERTIES INSTALL_NAME_DIR "@rpath"
        BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path" BUILD_WITH_INSTALL_RPATH ON)
    else()
      set_target_properties(cpkt_cmocka_shared PROPERTIES BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
    endif()
  endif()
  if(CPKT_BUILD_TESTS)
    cpkt_group_add_executable(cpkt_cmocka_behavior_${_variant} tests/cmocka_downstream_behavior.c)
    cpkt_configure_c89_target(cpkt_cmocka_behavior_${_variant})
    cpkt_add_repo_warning_errors(cpkt_cmocka_behavior_${_variant})
    cpkt_use_local_runtime(cpkt_cmocka_behavior_${_variant} cpkt::cmocka_shared)
    target_link_libraries(cpkt_cmocka_behavior_${_variant} PRIVATE cpkt_cmocka_${_variant})
    cpkt_group_add_test(NAME cmocka_c89_compile_${_variant}
      COMMAND "${CPKT_OPERATION_PYTHON}" "${CMAKE_SOURCE_DIR}/tests/cmocka_downstream_compile.py"
        "${CMAKE_C_COMPILER}" "${CMAKE_SOURCE_DIR}/tests/cmocka_downstream_behavior.c"
        "${CPKT_CMOCKA_FACADE_INCLUDE_DIR}" "${CPKT_EXTERNAL_ROOT}/cmocka/install/include"
        "${CMAKE_BINARY_DIR}/cmocka-c89-${_variant}.o")
    cpkt_group_add_executable(cpkt_cmocka_surface_${_variant} tests/cmocka_c89_surface.c)
    cpkt_configure_c89_target(cpkt_cmocka_surface_${_variant})
    cpkt_add_repo_warning_errors(cpkt_cmocka_surface_${_variant})
    cpkt_use_local_runtime(cpkt_cmocka_surface_${_variant} cpkt::cmocka_shared)
    target_link_libraries(cpkt_cmocka_surface_${_variant} PRIVATE cpkt_cmocka_${_variant})
    cpkt_group_add_test(NAME cmocka_surface_compile_${_variant}
      COMMAND "${CPKT_OPERATION_PYTHON}" "${CMAKE_SOURCE_DIR}/tests/cmocka_downstream_compile.py"
        "${CMAKE_C_COMPILER}" "${CMAKE_SOURCE_DIR}/tests/cmocka_c89_surface.c"
        "${CPKT_CMOCKA_FACADE_INCLUDE_DIR}" "${CPKT_EXTERNAL_ROOT}/cmocka/install/include"
        "${CMAKE_BINARY_DIR}/cmocka-surface-c89-${_variant}.o")
    if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
      cpkt_group_add_test(NAME cmocka_surface_${_variant} COMMAND cpkt_cmocka_surface_${_variant})
      set_tests_properties(cmocka_surface_${_variant} PROPERTIES LABELS "facade;memcheck")
      cpkt_group_add_test(NAME cmocka_behavior_${_variant} COMMAND cpkt_cmocka_behavior_${_variant})
      set_tests_properties(cmocka_behavior_${_variant} PROPERTIES LABELS "facade;memcheck")
      cpkt_group_add_test(NAME cmocka_failure_${_variant}
        COMMAND "${CPKT_OPERATION_PYTHON}" "${CMAKE_SOURCE_DIR}/tests/cmocka_downstream_failure.py"
          ${CPKT_TEST_EXECUTABLE_PREFIX} "$<TARGET_FILE:cpkt_cmocka_behavior_${_variant}>" failure)
    endif()
  endif()
endforeach()
