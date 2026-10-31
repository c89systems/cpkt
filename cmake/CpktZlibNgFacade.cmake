cpkt_group_get_target_property(CPKT_ZLIBNG_NATIVE_INCLUDE cpkt::zlib_ng_static INTERFACE_INCLUDE_DIRECTORIES)
get_filename_component(CPKT_ZLIB_NG_PREFIX "${CPKT_ZLIBNG_NATIVE_INCLUDE}" DIRECTORY)
set(CPKT_ZLIBNG_FACADE_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/zlibng")
set(CPKT_ZLIBNG_FACADE_INCLUDE_DIR "${CPKT_ZLIBNG_FACADE_GENERATED_DIR}/include")
set(CPKT_ZLIBNG_FACADE_HEADER "${CPKT_ZLIBNG_FACADE_INCLUDE_DIR}/cpkt/zlibng.h")
set(CPKT_ZLIBNG_FACADE_SOURCE "${CPKT_ZLIBNG_FACADE_GENERATED_DIR}/src/zlibng.c")
set(CPKT_ZLIBNG_FACADE_SURFACE "${CPKT_ZLIBNG_FACADE_GENERATED_DIR}/tests/surface.c")
cpkt_register_generated_outputs("${CPKT_ZLIBNG_FACADE_HEADER}" "${CPKT_ZLIBNG_FACADE_SOURCE}" "${CPKT_ZLIBNG_FACADE_SURFACE}")
add_custom_command(OUTPUT "${CPKT_ZLIBNG_FACADE_HEADER}" "${CPKT_ZLIBNG_FACADE_SOURCE}" "${CPKT_ZLIBNG_FACADE_SURFACE}"
  COMMAND "${CPKT_PYTHON3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/generate_zlibng_c89_facade.py"
    --include-dir "${CPKT_ZLIB_NG_PREFIX}/include"
    --header "${CPKT_ZLIBNG_FACADE_HEADER}" --source "${CPKT_ZLIBNG_FACADE_SOURCE}"
    --surface "${CPKT_ZLIBNG_FACADE_SURFACE}"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/generate_zlibng_c89_facade.py"
    "${CMAKE_SOURCE_DIR}/tools/generated_output_paths.py"
    "${CPKT_ZLIB_NG_PREFIX}/include/zlib-ng.h"
  VERBATIM)
cpkt_group_add_custom_target(cpkt_zlibng_facade_generated
  DEPENDS "${CPKT_ZLIBNG_FACADE_HEADER}" "${CPKT_ZLIBNG_FACADE_SOURCE}" "${CPKT_ZLIBNG_FACADE_SURFACE}")
foreach(variant static shared)
  if(variant STREQUAL "static")
    set(kind STATIC)
  else()
    set(kind SHARED)
  endif()
  cpkt_group_add_library(cpkt_zlibng_${variant} ${kind} "${CPKT_ZLIBNG_FACADE_SOURCE}")
  cpkt_group_target_command(add_dependencies cpkt_zlibng_${variant} cpkt_zlibng_facade_generated)
  cpkt_configure_c89_target(cpkt_zlibng_${variant})
  cpkt_add_repo_warning_errors(cpkt_zlibng_${variant})
  cpkt_group_target_command(target_include_directories cpkt_zlibng_${variant} PUBLIC
    "$<BUILD_INTERFACE:${CPKT_ZLIBNG_FACADE_INCLUDE_DIR}>")
  cpkt_group_target_command(target_link_libraries cpkt_zlibng_${variant} PRIVATE cpkt::zlib_ng_${variant})
  cpkt_group_set_target_properties(cpkt_zlibng_${variant} PROPERTIES
    OUTPUT_NAME cpkt_zlibng POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
  if(variant STREQUAL "shared")
    cpkt_apply_auth_export_catalog(cpkt_zlibng_shared cpkt_zlibng)
    cpkt_group_set_target_properties(cpkt_zlibng_shared PROPERTIES
      SOVERSION "${CPKT_ZLIBNG_ABI_VERSION}" VERSION "${PROJECT_VERSION}")
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
      cpkt_group_set_target_properties(cpkt_zlibng_shared PROPERTIES INSTALL_NAME_DIR "@rpath"
        BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path" BUILD_WITH_INSTALL_RPATH ON)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      cpkt_group_set_target_properties(cpkt_zlibng_shared PROPERTIES
        BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
    endif()
  endif()
endforeach()

if(CPKT_BUILD_TESTS)
  cpkt_group_add_test(NAME zlibng_exports COMMAND "${CMAKE_COMMAND}"
    "-DCPKT_NM=${CMAKE_NM}" "-DCPKT_SYSTEM=${CMAKE_SYSTEM_NAME}"
    "-DCPKT_NATIVE=$<TARGET_FILE:cpkt::zlib_ng_shared>"
    "-DCPKT_FACADE=$<TARGET_FILE:cpkt_zlibng_shared>"
    "-DCPKT_CATALOG=${CMAKE_SOURCE_DIR}/cmake/exports/cpkt_zlibng.txt"
    -P "${CMAKE_SOURCE_DIR}/tests/zlibng_exports_test.cmake")
  cpkt_group_set_tests_properties(zlibng_exports PROPERTIES LABELS "facade;packaging" TIMEOUT 30)
  foreach(variant static shared)
    foreach(case behavior surface)
      if(case STREQUAL "behavior")
        set(source "${CMAKE_SOURCE_DIR}/tests/zlibng_facade_test.c")
      else()
        set(source "${CPKT_ZLIBNG_FACADE_SURFACE}")
      endif()
      cpkt_group_add_executable(cpkt_zlibng_${case}_${variant} "${source}")
      cpkt_configure_c89_target(cpkt_zlibng_${case}_${variant})
      cpkt_add_repo_warning_errors(cpkt_zlibng_${case}_${variant})
      cpkt_group_target_command(target_link_libraries cpkt_zlibng_${case}_${variant} PRIVATE cpkt_zlibng_${variant})
      if(case STREQUAL "behavior")
        cpkt_group_target_command(target_link_libraries cpkt_zlibng_${case}_${variant} PRIVATE cpkt::zlib_static)
      endif()
      cpkt_group_use_local_runtime(cpkt_zlibng_${case}_${variant})
      if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
        cpkt_group_add_test(NAME zlibng_${case}_${variant} COMMAND cpkt_zlibng_${case}_${variant})
        cpkt_group_set_tests_properties(zlibng_${case}_${variant} PROPERTIES LABELS "facade;memcheck" TIMEOUT 60)
      endif()
    endforeach()
  endforeach()
  cpkt_group_add_executable(cpkt_zlibng_layout tests/zlibng_layout_test.c)
  cpkt_group_target_command(target_compile_options cpkt_zlibng_layout PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(cpkt_zlibng_layout)
  cpkt_group_target_command(target_link_libraries cpkt_zlibng_layout PRIVATE cpkt_zlibng_static cpkt::zlib_ng_static)
  cpkt_group_use_local_runtime(cpkt_zlibng_layout)
  if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
    cpkt_group_add_test(NAME zlibng_layout COMMAND cpkt_zlibng_layout)
    cpkt_group_set_tests_properties(zlibng_layout PROPERTIES LABELS "facade;memcheck" TIMEOUT 60)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    cpkt_group_add_executable(cpkt_zlibng_chorba_heap tests/zlibng_chorba_heap_test.c)
    cpkt_group_target_command(target_compile_options cpkt_zlibng_chorba_heap PRIVATE -std=c99)
    cpkt_group_target_command(target_include_directories cpkt_zlibng_chorba_heap PRIVATE
      "${CPKT_DEPENDENCY_BUILD_ROOT}/zlib-ng/src" "${CPKT_DEPENDENCY_BUILD_ROOT}/zlib-ng/build")
    cpkt_group_target_command(target_compile_definitions cpkt_zlibng_chorba_heap PRIVATE ZLIBNG_NATIVE_API HAVE_ATTRIBUTE_ALIGNED)
    if(CPKT_TARGET_ARCH STREQUAL "x86_64")
      cpkt_group_target_command(target_sources cpkt_zlibng_chorba_heap PRIVATE tests/zlibng_chorba_sse41_test.c)
      set_source_files_properties(tests/zlibng_chorba_sse41_test.c PROPERTIES COMPILE_OPTIONS -msse4.1)
      cpkt_group_target_command(target_compile_definitions cpkt_zlibng_chorba_heap PRIVATE CPKT_TEST_SSE41)
    endif()
    cpkt_group_target_command(target_link_libraries cpkt_zlibng_chorba_heap PRIVATE cpkt::zlib_ng_static Threads::Threads)
    cpkt_group_use_local_runtime(cpkt_zlibng_chorba_heap)
    if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
      cpkt_group_add_test(NAME zlibng_chorba_heap COMMAND cpkt_zlibng_chorba_heap)
      cpkt_group_set_tests_properties(zlibng_chorba_heap PROPERTIES LABELS "regression;memcheck" TIMEOUT 60)
    endif()
  endif()
endif()
