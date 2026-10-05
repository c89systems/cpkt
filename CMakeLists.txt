cmake_minimum_required(VERSION 3.21)
include(cmake/CpktGroups.cmake)
include(cmake/CpktOperation.cmake)
execute_process(
  COMMAND "${CMAKE_SOURCE_DIR}/scripts/release-version.sh" "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE CPKT_PROJECT_VERSION
  RESULT_VARIABLE CPKT_PROJECT_VERSION_RESULT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT CPKT_PROJECT_VERSION_RESULT EQUAL 0)
  message(FATAL_ERROR "failed to resolve cpkt release version")
endif()
project(cpkt VERSION "${CPKT_PROJECT_VERSION}" LANGUAGES C CXX)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CPKT_OSXCROSS_ROOT)
  set(_cpkt_osxcross_link_launcher "${_cpkt_build_launcher}")
  foreach(_cpkt_argument IN ITEMS "${CMAKE_COMMAND}" "-E" "env"
        "LD_LIBRARY_PATH=${CPKT_OSXCROSS_ROOT}/lib:$ENV{LD_LIBRARY_PATH}")
    string(REPLACE "'" "'\"'\"'" _cpkt_quoted_argument "${_cpkt_argument}")
    string(APPEND _cpkt_osxcross_link_launcher " '${_cpkt_quoted_argument}'")
  endforeach()
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK
      "${_cpkt_osxcross_link_launcher}")
endif()
if(CPKT_DEPENDENCY_PRODUCER AND CMAKE_EXPORT_COMPILE_COMMANDS)
  # Dependency-only graphs contain no project-owned compile commands.
  set(CMAKE_EXPORT_COMPILE_COMMANDS OFF)
endif()
include(CTest)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
option(CPKT_BUILD_DEPENDENCIES "Download and build pinned third-party dependencies." ON)
option(CPKT_BUILD_TESTS "Build ABI/link smoke tests for published dependency artifacts." ON)
option(CPKT_ALLOW_DEPENDENCY_ROOT_OVERRIDE "Honor explicitly provided dependency roots even when they differ from this compiler's default cache key." OFF)
option(CPKT_CALLER_OWNED_DEPENDENCY_ROOTS "Treat explicitly overridden dependency roots as caller-owned and skip repo-local lifecycle invalidation for them." OFF)
option(CPKT_ENABLE_FUZZING "Build AFL++ GCC-plugin facade fuzz targets." OFF)
option(CPKT_FACADE_ONLY "Build only repo-owned facade hardening targets." OFF)
if(CPKT_BUILD_DEPENDENCIES AND NOT CPKT_DEPENDENCY_PRODUCER)
  message(FATAL_ERROR "Use scripts/group-build.py configure/build to prepare the single owning producer; repository consumer graphs import successful output only")
endif()
option(CPKT_SUS_CPU_ONLY "Build the cpkt_sus whisper.cpp dependency set with CPU backends only." ON)
function(cpkt_normalize_arch arch out_var)
  string(TOLOWER "${arch}" _cpkt_arch)
  if(_cpkt_arch STREQUAL "amd64")
    set(_cpkt_arch "x86_64")
  elseif(_cpkt_arch STREQUAL "arm64")
    set(_cpkt_arch "aarch64")
  elseif(_cpkt_arch STREQUAL "arm")
    set(_cpkt_arch "armhf")
  endif()
  set(${out_var} "${_cpkt_arch}" PARENT_SCOPE)
endfunction()
include(cmake/CpktDependencyContract.cmake)
function(cpkt_target_executables_runnable out_var)
  string(TOLOWER "${CMAKE_SYSTEM_NAME}" _cpkt_target_os)
  string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" _cpkt_host_os)
  cpkt_normalize_arch("${CMAKE_SYSTEM_PROCESSOR}" _cpkt_target_arch)
  cpkt_normalize_arch("${CMAKE_HOST_SYSTEM_PROCESSOR}" _cpkt_host_arch)
  if(CMAKE_CROSSCOMPILING_EMULATOR)
    set(${out_var} ON PARENT_SCOPE)
  elseif(_cpkt_target_os STREQUAL _cpkt_host_os AND _cpkt_target_arch STREQUAL _cpkt_host_arch)
    set(${out_var} ON PARENT_SCOPE)
  else()
    set(${out_var} OFF PARENT_SCOPE)
  endif()
endfunction()
cpkt_target_executables_runnable(CPKT_CAN_RUN_TARGET_EXECUTABLES)
include(cmake/CpktLocalRuntime.cmake)
set(CPKT_TEST_EXECUTABLE_PREFIX)
if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
  set(CPKT_TEST_EXECUTABLE_PREFIX ${CMAKE_CROSSCOMPILING_EMULATOR})
endif()
function(cpkt_add_repo_warning_errors target_name)
  if(NOT TARGET "${target_name}")
    return()
  endif()
  get_target_property(_cpkt_target_type ${target_name} TYPE)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${target_name} PRIVATE
          -Wall
          -Wextra
          -Wpedantic
          -Werror
          "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=src"
          "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build"
          "-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=src"
          "-fdebug-prefix-map=${CMAKE_BINARY_DIR}=build")
    if(NOT _cpkt_target_type STREQUAL "STATIC_LIBRARY")
      if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_link_options(${target_name} PRIVATE LINKER:-fatal_warnings)
      else()
        target_link_options(${target_name} PRIVATE LINKER:--fatal-warnings)
      endif()
    endif()
  elseif(MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /WX)
    if(NOT _cpkt_target_type STREQUAL "STATIC_LIBRARY")
      target_link_options(${target_name} PRIVATE LINKER:/WX)
    endif()
  endif()
endfunction()
function(cpkt_configure_c89_target target_name)
  if(NOT TARGET "${target_name}")
    return()
  endif()
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${target_name} PRIVATE
          -std=c89
          -Wall
          -Wextra
          -Wpedantic
          -pedantic-errors)
  endif()
endfunction()
function(cpkt_apply_auth_export_catalog target_name catalog_name)
  if(NOT TARGET "${target_name}")
    return()
  endif()
  set(_catalog "${CMAKE_SOURCE_DIR}/cmake/exports/${catalog_name}.txt")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_catalog}")
  file(STRINGS "${_catalog}" _symbols REGEX "^cpkt_")
  if(NOT _symbols)
    message(FATAL_ERROR "empty authentication export catalog: ${_catalog}")
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_linker_file "${CMAKE_BINARY_DIR}/${catalog_name}.exports")
    set(_linker_contents "")
    foreach(_symbol IN LISTS _symbols)
      string(APPEND _linker_contents "_${_symbol}\n")
    endforeach()
    target_link_options(${target_name} PRIVATE
          "-Wl,-exported_symbols_list,${_linker_file}")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_linker_file "${CMAKE_BINARY_DIR}/${catalog_name}.map")
    set(_linker_contents "{\n  global:\n")
    foreach(_symbol IN LISTS _symbols)
      string(APPEND _linker_contents "    ${_symbol};\n")
    endforeach()
    string(APPEND _linker_contents "  local: *;\n};\n")
    target_link_options(${target_name} PRIVATE
          "-Wl,--version-script,${_linker_file}" "-Wl,--exclude-libs,ALL")
  endif()
  if(DEFINED _linker_file)
    if(EXISTS "${_linker_file}")
      file(READ "${_linker_file}" _previous_linker_contents)
    endif()
    if(NOT "${_previous_linker_contents}" STREQUAL "${_linker_contents}")
      file(WRITE "${_linker_file}" "${_linker_contents}")
    endif()
    set_property(TARGET ${target_name} APPEND PROPERTY LINK_DEPENDS
          "${_linker_file}")
  endif()
endfunction()
function(cpkt_configure_c89_lua_native_header_target target_name)
  if(NOT TARGET "${target_name}")
    return()
  endif()
  cpkt_configure_c89_target(${target_name})
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${target_name} PRIVATE
          -Wno-long-long
          -DLLONG_MAX=9223372036854775807LL
          -DLLONG_MIN=-9223372036854775807LL-1LL
          -DULLONG_MAX=18446744073709551615ULL)
  endif()
endfunction()
function(cpkt_add_lua_runtime_mock_test target_name source_name)
  cpkt_inventory_selected(targets "${target_name}" _selected)
  if(NOT _selected)
    return()
  endif()
  cpkt_group_add_executable(${target_name}
      src/lua_runtime.c
      tests/lua_mock/lua_mock.c
      ${source_name})
  cpkt_group_use_local_runtime(${target_name})
  target_compile_options(${target_name} PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(${target_name})
  target_include_directories(${target_name} PRIVATE
      "${CMAKE_SOURCE_DIR}/tests/lua_mock/include"
      "${CMAKE_SOURCE_DIR}/include")
endfunction()
function(cpkt_configure_afl_target target_name)
  if(NOT TARGET "${target_name}")
    return()
  endif()
  if(NOT DEFINED CPKT_AFLPP_ROOT OR "${CPKT_AFLPP_ROOT}" STREQUAL "")
    message(FATAL_ERROR
          "CPKT_ENABLE_FUZZING requires the pinned AFL++ toolchain; "
          "configure with the fuzz or opcua-fuzz preset.")
  endif()
  if(NOT EXISTS "${CPKT_AFLPP_ROOT}/bin/afl-fuzz" OR
        NOT EXISTS "${CPKT_AFLPP_ROOT}/lib/afl/afl-gcc-pass.so")
    message(FATAL_ERROR
          "CPKT_ENABLE_FUZZING requires a complete pinned AFL++ toolchain at "
          "CPKT_AFLPP_ROOT=${CPKT_AFLPP_ROOT}.")
  endif()
  if(NOT CMAKE_C_COMPILER MATCHES "/cpkt-afl-gcc$" OR
        NOT CMAKE_CXX_COMPILER MATCHES "/cpkt-afl-g\\+\\+$")
    message(FATAL_ERROR
          "CPKT_ENABLE_FUZZING requires the pinned AFL++ compiler wrappers; "
          "configure with the fuzz or opcua-fuzz preset.")
  endif()
  target_sources(${target_name} PRIVATE "${CMAKE_SOURCE_DIR}/fuzz/afl_input_driver.c")
  target_compile_options(${target_name} PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(${target_name})
endfunction()
function(cpkt_register_local_runtime_checks)
  get_property(_cpkt_local_tests DIRECTORY PROPERTY TESTS)
  foreach(_cpkt_test IN LISTS _cpkt_local_tests)
    get_test_property(${_cpkt_test} ENVIRONMENT _cpkt_test_environment)
    if(_cpkt_test_environment MATCHES "(^|;)LD_LIBRARY_PATH=")
      message(FATAL_ERROR "Native test ${_cpkt_test} exports LD_LIBRARY_PATH; use private executable runtime paths")
    endif()
  endforeach()
  if(TARGET cpkt_audio_shared)
  endif()
  get_property(_cpkt_local_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  set(_cpkt_local_executables)
  foreach(_cpkt_target IN LISTS _cpkt_local_targets)
    get_target_property(_cpkt_type ${_cpkt_target} TYPE)
    if(_cpkt_type STREQUAL "EXECUTABLE")
      list(APPEND _cpkt_local_executables "$<TARGET_FILE:${_cpkt_target}>")
    endif()
  endforeach()
  if(CPKT_COMPOSITION_ONLY)
    foreach(_group core db misc)
      file(GLOB _cpkt_owner_files "${CMAKE_SOURCE_DIR}/build/${CPKT_TARGET_ID}/${_group}/${CMAKE_BUILD_TYPE}/*")
      foreach(_file IN LISTS _cpkt_owner_files)
        if(NOT IS_DIRECTORY "${_file}")
          file(READ "${_file}" _magic LIMIT 4 HEX)
          if(_magic STREQUAL "7f454c46" AND NOT _file MATCHES "[.](so([.][0-9]+)*|o)$")
            list(APPEND _cpkt_local_executables "${_file}")
          endif()
        endif()
      endforeach()
    endforeach()
  endif()
  set(_cpkt_runtime_policy_name local_executable_runtime_policy)
  if(NOT CPKT_GROUP STREQUAL "core")
    string(APPEND _cpkt_runtime_policy_name "_${CPKT_GROUP}")
  endif()
  cpkt_group_add_test(NAME ${_cpkt_runtime_policy_name}
      COMMAND bash "${CMAKE_SOURCE_DIR}/tests/local_executable_runtime_policy_test.sh"
        "${CPKT_RUNTIME_LOADER}" ${_cpkt_local_executables})
endfunction()
if(NOT CPKT_DEPENDENCY_PRODUCER)
  add_custom_target(cpkt_operation_guard
    COMMAND "${CPKT_OPERATION_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/cpkt_operation.py"
      --root "${CMAKE_SOURCE_DIR}" --group "${CPKT_GROUP}" --check
    VERBATIM)
  cmake_language(DEFER CALL cpkt_validate_owned_graph)
endif()
if(CPKT_BUILD_TESTS AND CPKT_NATIVE_BOOTLIN_RUNTIME)
  cmake_language(DEFER CALL cpkt_register_local_runtime_checks)
  cpkt_group_add_executable(cpkt_bootlin_runtime_test tests/bootlin_runtime_test.c)
  target_compile_options(cpkt_bootlin_runtime_test PRIVATE -std=c89)
  cpkt_add_repo_warning_errors(cpkt_bootlin_runtime_test)
  cpkt_group_use_local_runtime(cpkt_bootlin_runtime_test)
  cpkt_group_add_test(NAME bootlin_runtime_process_boundary
      COMMAND cpkt_bootlin_runtime_test "${CMAKE_SYSROOT}")
  cpkt_group_add_test(NAME local_runtime_rejects_wrong_loader
      COMMAND bash "${CMAKE_SOURCE_DIR}/tests/local_executable_runtime_policy_test.sh"
        "${CPKT_RUNTIME_LOADER}.wrong" "$<TARGET_FILE:cpkt_bootlin_runtime_test>")
  cpkt_group_set_tests_properties(local_runtime_rejects_wrong_loader PROPERTIES WILL_FAIL TRUE)
endif()
if(CPKT_FACADE_ONLY AND CPKT_ENABLE_FUZZING AND CPKT_GROUP STREQUAL "core")
  foreach(_cpkt_facade_only_cache_var
        CPKT_DIST_DIR
        CPKT_TARGET_ARCH
        CPKT_TARGET_LIBC
        CPKT_TARGET_OS)
    if(DEFINED ${_cpkt_facade_only_cache_var})
      set(_cpkt_facade_only_ignored "${${_cpkt_facade_only_cache_var}}")
    endif()
  endforeach()
  unset(_cpkt_facade_only_ignored)
  if(NOT CPKT_BUILD_TESTS)
    message(FATAL_ERROR "CPKT_FACADE_ONLY requires CPKT_BUILD_TESTS=ON")
  endif()
  cpkt_add_lua_runtime_mock_test(cpkt_lua_runtime_mock_test tests/lua_runtime_mock_test.c)
  if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
    cpkt_group_add_test(NAME lua_runtime_mock_unit COMMAND cpkt_lua_runtime_mock_test)
  endif()
  if(CPKT_ENABLE_FUZZING)
    cpkt_add_lua_runtime_mock_test(cpkt_lua_runtime_fuzz fuzz/lua_runtime_fuzz.c)
    cpkt_configure_afl_target(cpkt_lua_runtime_fuzz)
  endif()
  return()
endif()
if(DEFINED CPKT_BUILD_STATIC AND NOT CPKT_BUILD_STATIC)
  message(FATAL_ERROR "cpkt release bundles always include static and shared artifacts; CPKT_BUILD_STATIC cannot be disabled.")
endif()
if(DEFINED CPKT_BUILD_SHARED AND NOT CPKT_BUILD_SHARED)
  message(FATAL_ERROR "cpkt release bundles always include static and shared artifacts; CPKT_BUILD_SHARED cannot be disabled.")
endif()
set(CPKT_BUILD_STATIC ON CACHE INTERNAL "cpkt release bundles always include static artifacts.")
set(CPKT_BUILD_SHARED ON CACHE INTERNAL "cpkt release bundles always include shared artifacts.")
set(CPKT_BUNDLE_VERSION "${PROJECT_VERSION}" CACHE STRING "Published dependency bundle version." FORCE)
set(CPKT_OPENSSL_VERSION "3.6.4" CACHE STRING "Pinned OpenSSL release.")
set(CPKT_OPENSSL_BUILD_CONFIG_REVISION "origin-runpath-1" CACHE INTERNAL "OpenSSL Configure contract revision." FORCE)
set(CPKT_ZLIB_VERSION "1.3.2" CACHE STRING "Pinned zlib release.")
set(CPKT_CURL_VERSION "8.22.0" CACHE STRING "Pinned curl release.")
set(CPKT_NGHTTP2_VERSION "1.70.0" CACHE STRING "Pinned nghttp2 release.")
set(CPKT_LIBSSH2_VERSION "1.11.1" CACHE STRING "Pinned libssh2 release.")
set(CPKT_CMOCKA_VERSION "2.0.2" CACHE STRING "Pinned cmocka release.")
set(CPKT_LIBXML2_VERSION "2.15.4" CACHE STRING "Pinned libxml2 release.")
set(CPKT_LUA_VERSION "5.5.1" CACHE STRING "Pinned Lua release.")
set(CPKT_MQTTC_VERSION "1.1.2" CACHE STRING "Pinned MQTT-C project version.")
set(CPKT_MQTTC_COMMIT "0f4c34c8cc00b16cfee094745d68b8cdbaecd8e0" CACHE STRING "Pinned MQTT-C commit used by open62541 v1.5.8.")
set(CPKT_KRB5_VERSION "1.22.2" CACHE STRING "Pinned MIT Kerberos release.")
set(CPKT_CYRUS_SASL_VERSION "2.1.28" CACHE STRING "Pinned Cyrus SASL release.")
set(CPKT_OPENLDAP_VERSION "2.6.15" CACHE STRING "Pinned OpenLDAP LTS release.")
set(CPKT_LUA_ABI_VERSION "0" CACHE STRING "Lua C89 facade ABI major. Do not bump without maintainer approval.")
set(CPKT_LUA_RUNTIME_ABI_VERSION "0" CACHE STRING "Lua runtime facade ABI major. Do not bump without maintainer approval.")
set(CPKT_GSSAPI_ABI_VERSION "0" CACHE STRING "GSSAPI facade ABI major. Do not bump without maintainer approval.")
set(CPKT_SASL_ABI_VERSION "0" CACHE STRING "Cyrus SASL facade ABI major. Do not bump without maintainer approval.")
set(CPKT_OPENSSL_ABI_VERSION "0" CACHE STRING "OpenSSL C89 facade ABI major. Do not bump without maintainer approval.")
set(CPKT_MQTTC_ABI_VERSION "0" CACHE STRING "MQTT-C facade ABI major. Do not bump without maintainer approval.")
set(CPKT_NGHTTP2_ABI_VERSION "0" CACHE STRING "nghttp2 C89 facade ABI major. Do not bump without maintainer approval.")
set(CPKT_LIBSSH2_ABI_VERSION "0" CACHE STRING "libssh2 C89 facade ABI major. Do not bump without maintainer approval.")
if(NOT CPKT_SUS_CPU_ONLY)
  message(FATAL_ERROR "CPKT_SUS_CPU_ONLY=OFF is reserved for a later GPU-backend packaging phase")
endif()
set(CPKT_SUS_BACKEND_CAPABILITIES "cpu" CACHE INTERNAL "Compiled cpkt_sus whisper.cpp backend capability list.")
set(CPKT_CXX_STDLIB_STATIC_LIBRARY "" CACHE FILEPATH "Static C++ standard library archive used by C-facing static SDK metadata.")
if(CPKT_CXX_STDLIB_STATIC_LIBRARY STREQUAL "" AND CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
  execute_process(
      COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libstdc++.a
      RESULT_VARIABLE _cpkt_cxx_stdlib_result
      OUTPUT_VARIABLE _cpkt_cxx_stdlib_output
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(_cpkt_cxx_stdlib_result EQUAL 0 AND
        NOT _cpkt_cxx_stdlib_output STREQUAL "" AND
        NOT _cpkt_cxx_stdlib_output STREQUAL "libstdc++.a" AND
        EXISTS "${_cpkt_cxx_stdlib_output}")
    set(CPKT_CXX_STDLIB_STATIC_LIBRARY "${_cpkt_cxx_stdlib_output}" CACHE FILEPATH "Static C++ standard library archive used by C-facing static SDK metadata." FORCE)
  endif()
endif()
set(CPKT_CXX_LIBGCC_STATIC_LIBRARY "" CACHE FILEPATH "Static GCC runtime archive used by C-facing static SDK metadata.")
if(CPKT_CXX_LIBGCC_STATIC_LIBRARY STREQUAL "" AND CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
  execute_process(
      COMMAND "${CMAKE_CXX_COMPILER}" -print-libgcc-file-name
      RESULT_VARIABLE _cpkt_cxx_libgcc_result
      OUTPUT_VARIABLE _cpkt_cxx_libgcc_output
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(_cpkt_cxx_libgcc_result EQUAL 0 AND
        NOT _cpkt_cxx_libgcc_output STREQUAL "" AND
        NOT _cpkt_cxx_libgcc_output STREQUAL "libgcc.a" AND
        EXISTS "${_cpkt_cxx_libgcc_output}")
    set(CPKT_CXX_LIBGCC_STATIC_LIBRARY "${_cpkt_cxx_libgcc_output}" CACHE FILEPATH "Static GCC runtime archive used by C-facing static SDK metadata." FORCE)
  endif()
endif()
include(cmake/CpktDependencyArchiveCache.cmake)
if(CPKT_BUILD_DEPENDENCIES OR CPKT_BUILD_TESTS)
  cpkt_initialize_dependency_cache()
endif()
set(CPKT_DEPENDENCY_CACHE_LOCK_TIMEOUT "120" CACHE STRING "Maximum shared dependency archive cache lock wait in seconds.")
set(CPKT_DOWNLOAD_ROOT "${CMAKE_SOURCE_DIR}/.cache/downloads" CACHE PATH "Local ExternalProject archive staging root.")
set(CPKT_DIST_DIR "${CMAKE_SOURCE_DIR}/dist" CACHE PATH "Output directory for package archives.")
set(CPKT_TARGET_ARCH "${CMAKE_SYSTEM_PROCESSOR}" CACHE STRING "Artifact architecture name.")
set(CPKT_TARGET_OS "${CMAKE_SYSTEM_NAME}" CACHE STRING "Artifact operating system name.")
set(CPKT_TARGET_LIBC "" CACHE STRING "Artifact libc suffix for Linux targets.")
set(CPKT_DEPENDENCY_BUILD_TYPE "Release")
set(CPKT_DEPENDENCY_BUILD_JOBS_DEFAULT 8)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  set(CPKT_DEPENDENCY_BUILD_JOBS_DEFAULT 2)
endif()
if(NOT CPKT_DEPENDENCY_BUILD_JOBS_DEFAULT OR CPKT_DEPENDENCY_BUILD_JOBS_DEFAULT LESS 1)
  set(CPKT_DEPENDENCY_BUILD_JOBS_DEFAULT 1)
endif()
set(CPKT_DEPENDENCY_BUILD_JOBS "${CPKT_DEPENDENCY_BUILD_JOBS_DEFAULT}" CACHE STRING "Parallel dependency build job count.")
set(_cpkt_job_limit 8)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  set(_cpkt_job_limit 2)
endif()
if(NOT CPKT_DEPENDENCY_BUILD_JOBS MATCHES "^[1-9][0-9]*$" OR
    CPKT_DEPENDENCY_BUILD_JOBS GREATER _cpkt_job_limit)
  message(FATAL_ERROR "CPKT_DEPENDENCY_BUILD_JOBS must be between 1 and ${_cpkt_job_limit} on this host")
endif()
set(CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT "300" CACHE STRING "Maximum source archive download time in seconds.")
set(CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT "60" CACHE STRING "Maximum source archive idle download time in seconds.")
set(CPKT_DEPENDENCY_DOWNLOAD_RETRIES "3" CACHE STRING "Bounded retry count for each dependency archive URL.")
string(TOLOWER "${CPKT_TARGET_OS}" CPKT_TARGET_OS_LOWER)
cpkt_normalize_arch("${CPKT_TARGET_ARCH}" CPKT_TARGET_ARCH_LOWER)
if(CPKT_TARGET_OS_LOWER STREQUAL "darwin" AND CPKT_TARGET_ARCH_LOWER STREQUAL "aarch64")
  set(CPKT_TARGET_ARCH_LOWER "arm64")
endif()
set(CPKT_TARGET_ARCH "${CPKT_TARGET_ARCH_LOWER}" CACHE STRING "Artifact architecture name." FORCE)
if(CPKT_TARGET_OS_LOWER STREQUAL "linux" AND NOT CPKT_TARGET_LIBC)
  execute_process(
      COMMAND "${CMAKE_C_COMPILER}" -dumpmachine
      OUTPUT_VARIABLE cpkt_compiler_machine
      RESULT_VARIABLE cpkt_compiler_machine_result
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
  if(cpkt_compiler_machine_result EQUAL 0 AND cpkt_compiler_machine MATCHES "musl")
    set(CPKT_TARGET_LIBC "musl")
  else()
    set(CPKT_TARGET_LIBC "gnu")
  endif()
endif()
if(CPKT_TARGET_OS_LOWER STREQUAL "darwin")
  set(CPKT_TARGET_ID "${CPKT_TARGET_ARCH_LOWER}-apple-darwin")
elseif(CPKT_TARGET_OS_LOWER STREQUAL "linux")
  set(CPKT_TARGET_ID "${CPKT_TARGET_ARCH_LOWER}-linux-${CPKT_TARGET_LIBC}")
else()
  set(CPKT_TARGET_ID "${CPKT_TARGET_ARCH_LOWER}-${CPKT_TARGET_OS_LOWER}")
endif()
set(CPKT_TARGET_ID "${CPKT_TARGET_ID}" CACHE INTERNAL "Resolved SDK target identity." FORCE)
set(_cpkt_default_external_root "${CMAKE_SOURCE_DIR}/.cache/deps/${CPKT_TARGET_ID}")
set(_cpkt_default_dependency_build_root "${CMAKE_SOURCE_DIR}/.cache/deps-build/${CPKT_TARGET_ID}")
set(CPKT_DEPENDENCY_CONTRACT_ROOT "${CMAKE_SOURCE_DIR}/.cache/dependency-contracts" CACHE PATH "Target-local dependency build contract metadata.")
set(_cpkt_external_root_explicit OFF)
if(DEFINED CPKT_EXTERNAL_ROOT)
  set(_cpkt_external_root_explicit ON)
  set(_cpkt_external_root_original "${CPKT_EXTERNAL_ROOT}")
endif()
set(_cpkt_dependency_build_root_explicit OFF)
if(DEFINED CPKT_DEPENDENCY_BUILD_ROOT)
  set(_cpkt_dependency_build_root_explicit ON)
  set(_cpkt_dependency_build_root_original "${CPKT_DEPENDENCY_BUILD_ROOT}")
endif()
if(DEFINED CPKT_EXTERNAL_ROOT)
  string(FIND "${CPKT_EXTERNAL_ROOT}" "${CMAKE_SOURCE_DIR}/.cache/deps/" _cpkt_external_root_under_cache)
else()
  set(_cpkt_external_root_under_cache -1)
endif()
if(NOT DEFINED CPKT_EXTERNAL_ROOT
    OR "${CPKT_EXTERNAL_ROOT}" STREQUAL "${CMAKE_SOURCE_DIR}/.cache/deps"
    OR (NOT CPKT_ALLOW_DEPENDENCY_ROOT_OVERRIDE
      AND _cpkt_external_root_under_cache EQUAL 0
      AND NOT "${CPKT_EXTERNAL_ROOT}" STREQUAL "${_cpkt_default_external_root}"))
  set(CPKT_EXTERNAL_ROOT "${_cpkt_default_external_root}" CACHE PATH "Target-local install root for dependency artifacts." FORCE)
endif()
if(_cpkt_external_root_explicit
    AND (NOT "${_cpkt_external_root_original}" STREQUAL "${CPKT_EXTERNAL_ROOT}"
      OR "${CPKT_EXTERNAL_ROOT}" STREQUAL "${_cpkt_default_external_root}"))
  set(_cpkt_external_root_explicit OFF)
endif()
if(DEFINED CPKT_DEPENDENCY_BUILD_ROOT)
  string(FIND "${CPKT_DEPENDENCY_BUILD_ROOT}" "${CMAKE_SOURCE_DIR}/.cache/deps-build/" _cpkt_dependency_build_root_under_cache)
else()
  set(_cpkt_dependency_build_root_under_cache -1)
endif()
if(NOT DEFINED CPKT_DEPENDENCY_BUILD_ROOT
    OR "${CPKT_DEPENDENCY_BUILD_ROOT}" STREQUAL "${CMAKE_SOURCE_DIR}/.cache/deps-build"
    OR (NOT CPKT_ALLOW_DEPENDENCY_ROOT_OVERRIDE
      AND _cpkt_dependency_build_root_under_cache EQUAL 0
      AND NOT "${CPKT_DEPENDENCY_BUILD_ROOT}" STREQUAL "${_cpkt_default_dependency_build_root}"))
  set(CPKT_DEPENDENCY_BUILD_ROOT "${_cpkt_default_dependency_build_root}" CACHE PATH "Target-local build root for dependency artifacts." FORCE)
endif()
if(_cpkt_dependency_build_root_explicit
    AND (NOT "${_cpkt_dependency_build_root_original}" STREQUAL "${CPKT_DEPENDENCY_BUILD_ROOT}"
      OR "${CPKT_DEPENDENCY_BUILD_ROOT}" STREQUAL "${_cpkt_default_dependency_build_root}"))
  set(_cpkt_dependency_build_root_explicit OFF)
endif()
set(CPKT_EXTERNAL_ROOT_LIFECYCLE_OWNED ON)
if(CPKT_ALLOW_DEPENDENCY_ROOT_OVERRIDE
    AND CPKT_CALLER_OWNED_DEPENDENCY_ROOTS
    AND _cpkt_external_root_explicit)
  set(CPKT_EXTERNAL_ROOT_LIFECYCLE_OWNED OFF)
endif()
set(CPKT_DEPENDENCY_BUILD_ROOT_LIFECYCLE_OWNED ON)
if(CPKT_ALLOW_DEPENDENCY_ROOT_OVERRIDE
    AND CPKT_CALLER_OWNED_DEPENDENCY_ROOTS
    AND _cpkt_dependency_build_root_explicit)
  set(CPKT_DEPENDENCY_BUILD_ROOT_LIFECYCLE_OWNED OFF)
endif()
set(_cpkt_inventory_arguments "")
if(NOT CPKT_BUILD_TESTS)
  list(APPEND _cpkt_inventory_arguments --no-tests)
endif()
execute_process(COMMAND "${CPKT_OPERATION_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/cpkt_inventory_cli.py"
  --root "${CMAKE_SOURCE_DIR}" --group "${CPKT_GROUP}" --closure --cmake ${_cpkt_inventory_arguments}
  OUTPUT_VARIABLE CPKT_ACTIVE_COMPONENTS OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _cpkt_inventory_status)
if(NOT _cpkt_inventory_status EQUAL 0)
  message(FATAL_ERROR "Invalid component inventory")
endif()
if(CPKT_GROUP MATCHES "^(db|misc)$")
  if(NOT CPKT_PREREQUISITE_CONFIGURATION)
    set(CPKT_PREREQUISITE_CONFIGURATION "${CMAKE_BUILD_TYPE}")
  endif()
  execute_process(COMMAND "${CPKT_OPERATION_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/cpkt_receipt_cli.py"
      --root "${CMAKE_SOURCE_DIR}" --group "${CPKT_GROUP}" --target "${CPKT_TARGET_ID}"
      --configuration "${CPKT_PREREQUISITE_CONFIGURATION}" --preset "$ENV{CPKT_PRESET}"
      RESULT_VARIABLE _core_status)
  if(NOT _core_status EQUAL 0)
    message(FATAL_ERROR "Core prerequisite is not development-ready")
  endif()
endif()
if(CPKT_ENABLE_FUZZING)
  if(NOT CPKT_AFLPP_ROOT)
    message(FATAL_ERROR "CPKT_ENABLE_FUZZING requires the pinned AFL++ toolchain")
  endif()
  if(NOT CMAKE_C_COMPILER MATCHES "/cpkt-afl-gcc$" OR
        NOT CMAKE_CXX_COMPILER MATCHES "/cpkt-afl-g[+][+]$")
    message(FATAL_ERROR "CPKT_ENABLE_FUZZING requires the pinned AFL++ compiler wrappers")
  endif()
endif()
include(cmake/CpktDependencies.cmake)
cpkt_configure_dependencies()
if(CPKT_DEPENDENCY_PRODUCER)
  return()
endif()
find_program(CPKT_PYTHON3_EXECUTABLE NAMES python3 REQUIRED)
set(Python3_EXECUTABLE "${CPKT_PYTHON3_EXECUTABLE}")
include(cmake/CpktCmockaFacade.cmake)
set(CPKT_NGHTTP2_FACADE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/nghttp2")
set(CPKT_NGHTTP2_FACADE_INCLUDE_DIR "${CPKT_NGHTTP2_FACADE_GENERATED_DIR}/include")
set(CPKT_NGHTTP2_FACADE_HEADER "${CPKT_NGHTTP2_FACADE_INCLUDE_DIR}/cpkt/nghttp2.h")
set(CPKT_NGHTTP2_FACADE_SOURCE "${CPKT_NGHTTP2_FACADE_GENERATED_DIR}/src/nghttp2.c")
cpkt_group_get_target_property(CPKT_NGHTTP2_FACADE_NATIVE_INCLUDE_DIR
  cpkt::nghttp2_static INTERFACE_INCLUDE_DIRECTORIES)
if(NOT CPKT_NGHTTP2_FACADE_NATIVE_INCLUDE_DIR)
  message(FATAL_ERROR "nghttp2 target does not expose public include directories")
endif()
set(CPKT_NGHTTP2_FACADE_NATIVE_HEADERS "${CPKT_NGHTTP2_FACADE_NATIVE_INCLUDE_DIR}/nghttp2/nghttp2.h" "${CPKT_NGHTTP2_FACADE_NATIVE_INCLUDE_DIR}/nghttp2/nghttp2ver.h")
add_custom_command(
  OUTPUT "${CPKT_NGHTTP2_FACADE_HEADER}" "${CPKT_NGHTTP2_FACADE_SOURCE}"
  COMMAND "${CPKT_PYTHON3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tools/generate_nghttp2_c89_facade.py"
    --include-dir "$<TARGET_PROPERTY:cpkt::nghttp2_static,INTERFACE_INCLUDE_DIRECTORIES>"
    --header "${CPKT_NGHTTP2_FACADE_HEADER}"
    --source "${CPKT_NGHTTP2_FACADE_SOURCE}"
  DEPENDS
    "${CMAKE_SOURCE_DIR}/tools/generate_nghttp2_c89_facade.py"
    ${CPKT_NGHTTP2_FACADE_NATIVE_HEADERS}
  VERBATIM)
set_source_files_properties("${CPKT_NGHTTP2_FACADE_SOURCE}" PROPERTIES GENERATED TRUE)
cpkt_group_add_custom_target(cpkt_nghttp2_facade_generated
  DEPENDS "${CPKT_NGHTTP2_FACADE_HEADER}" "${CPKT_NGHTTP2_FACADE_SOURCE}")
if(TARGET cpkt_nghttp2_project)
  cpkt_group_target_command(add_dependencies cpkt_nghttp2_facade_generated cpkt_nghttp2_project)
endif()
cpkt_group_add_library(cpkt_nghttp2_static STATIC "${CPKT_NGHTTP2_FACADE_SOURCE}")
cpkt_group_target_command(add_dependencies cpkt_nghttp2_static cpkt_nghttp2_facade_generated)
cpkt_configure_c89_target(cpkt_nghttp2_static)
cpkt_add_repo_warning_errors(cpkt_nghttp2_static)
cpkt_group_target_command(target_compile_definitions cpkt_nghttp2_static PRIVATE CPKT_NGHTTP2_STATIC)
cpkt_group_target_command(target_include_directories cpkt_nghttp2_static PUBLIC
  "$<BUILD_INTERFACE:${CPKT_NGHTTP2_FACADE_INCLUDE_DIR}>")
cpkt_group_target_command(target_link_libraries cpkt_nghttp2_static PUBLIC cpkt::nghttp2_static)
cpkt_group_set_target_properties(cpkt_nghttp2_static PROPERTIES OUTPUT_NAME cpkt_nghttp2 POSITION_INDEPENDENT_CODE ON)
cpkt_group_add_library(cpkt_nghttp2_shared SHARED "${CPKT_NGHTTP2_FACADE_SOURCE}")
cpkt_group_target_command(add_dependencies cpkt_nghttp2_shared cpkt_nghttp2_facade_generated)
cpkt_configure_c89_target(cpkt_nghttp2_shared)
cpkt_add_repo_warning_errors(cpkt_nghttp2_shared)
cpkt_group_target_command(target_compile_definitions cpkt_nghttp2_shared PRIVATE CPKT_NGHTTP2_BUILDING_SHARED)
cpkt_group_target_command(target_include_directories cpkt_nghttp2_shared PUBLIC
  "$<BUILD_INTERFACE:${CPKT_NGHTTP2_FACADE_INCLUDE_DIR}>")
cpkt_group_target_command(target_link_libraries cpkt_nghttp2_shared PUBLIC cpkt::nghttp2_shared)
cpkt_group_set_target_properties(cpkt_nghttp2_shared PROPERTIES OUTPUT_NAME cpkt_nghttp2 C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON SOVERSION "${CPKT_NGHTTP2_ABI_VERSION}" VERSION "${PROJECT_VERSION}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  cpkt_group_set_target_properties(cpkt_nghttp2_shared PROPERTIES INSTALL_NAME_DIR "@rpath" BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path" BUILD_WITH_INSTALL_RPATH ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cpkt_group_set_target_properties(cpkt_nghttp2_shared PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
endif()
set(CPKT_LIBSSH2_FACADE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/libssh2")
set(CPKT_LIBSSH2_FACADE_INCLUDE_DIR "${CPKT_LIBSSH2_FACADE_GENERATED_DIR}/include")
set(CPKT_LIBSSH2_FACADE_HEADER "${CPKT_LIBSSH2_FACADE_INCLUDE_DIR}/cpkt/libssh2.h")
set(CPKT_LIBSSH2_FACADE_SOURCE "${CPKT_LIBSSH2_FACADE_GENERATED_DIR}/src/libssh2.c")
cpkt_group_get_target_property(CPKT_LIBSSH2_FACADE_NATIVE_INCLUDE_DIR
  cpkt::libssh2_static INTERFACE_INCLUDE_DIRECTORIES)
if(NOT CPKT_LIBSSH2_FACADE_NATIVE_INCLUDE_DIR)
  message(FATAL_ERROR "libssh2 target does not expose public include directories")
endif()
set(CPKT_LIBSSH2_FACADE_NATIVE_HEADERS "${CPKT_LIBSSH2_FACADE_NATIVE_INCLUDE_DIR}/libssh2.h" "${CPKT_LIBSSH2_FACADE_NATIVE_INCLUDE_DIR}/libssh2_sftp.h" "${CPKT_LIBSSH2_FACADE_NATIVE_INCLUDE_DIR}/libssh2_publickey.h")
add_custom_command(
  OUTPUT "${CPKT_LIBSSH2_FACADE_HEADER}" "${CPKT_LIBSSH2_FACADE_SOURCE}"
  COMMAND "${CPKT_PYTHON3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tools/generate_libssh2_c89_facade.py"
    --include-dir "$<TARGET_PROPERTY:cpkt::libssh2_static,INTERFACE_INCLUDE_DIRECTORIES>"
    --header "${CPKT_LIBSSH2_FACADE_HEADER}"
    --source "${CPKT_LIBSSH2_FACADE_SOURCE}"
  DEPENDS
    "${CMAKE_SOURCE_DIR}/tools/generate_libssh2_c89_facade.py"
    ${CPKT_LIBSSH2_FACADE_NATIVE_HEADERS}
  VERBATIM)
set_source_files_properties("${CPKT_LIBSSH2_FACADE_SOURCE}" PROPERTIES GENERATED TRUE)
cpkt_group_add_custom_target(cpkt_libssh2_facade_generated
  DEPENDS "${CPKT_LIBSSH2_FACADE_HEADER}" "${CPKT_LIBSSH2_FACADE_SOURCE}")
if(TARGET cpkt_libssh2_project)
  cpkt_group_target_command(add_dependencies cpkt_libssh2_facade_generated cpkt_libssh2_project)
endif()
cpkt_group_add_library(cpkt_libssh2_static STATIC "${CPKT_LIBSSH2_FACADE_SOURCE}")
cpkt_group_target_command(add_dependencies cpkt_libssh2_static cpkt_libssh2_facade_generated)
cpkt_configure_c89_target(cpkt_libssh2_static)
cpkt_add_repo_warning_errors(cpkt_libssh2_static)
cpkt_group_target_command(target_compile_definitions cpkt_libssh2_static PRIVATE CPKT_LIBSSH2_STATIC)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  cpkt_group_target_command(target_compile_options cpkt_libssh2_static PRIVATE
      -Wno-long-long
      -Wno-deprecated-declarations)
endif()
cpkt_group_target_command(target_include_directories cpkt_libssh2_static PUBLIC
  "$<BUILD_INTERFACE:${CPKT_LIBSSH2_FACADE_INCLUDE_DIR}>")
cpkt_group_target_command(target_link_libraries cpkt_libssh2_static PUBLIC cpkt::libssh2_static)
cpkt_group_set_target_properties(cpkt_libssh2_static PROPERTIES OUTPUT_NAME cpkt_libssh2 POSITION_INDEPENDENT_CODE ON)
cpkt_group_add_library(cpkt_libssh2_shared SHARED "${CPKT_LIBSSH2_FACADE_SOURCE}")
cpkt_group_target_command(add_dependencies cpkt_libssh2_shared cpkt_libssh2_facade_generated)
cpkt_configure_c89_target(cpkt_libssh2_shared)
cpkt_add_repo_warning_errors(cpkt_libssh2_shared)
cpkt_group_target_command(target_compile_definitions cpkt_libssh2_shared PRIVATE CPKT_LIBSSH2_BUILDING_SHARED)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  cpkt_group_target_command(target_compile_options cpkt_libssh2_shared PRIVATE
      -Wno-long-long
      -Wno-deprecated-declarations)
endif()
cpkt_group_target_command(target_include_directories cpkt_libssh2_shared PUBLIC
  "$<BUILD_INTERFACE:${CPKT_LIBSSH2_FACADE_INCLUDE_DIR}>")
cpkt_group_target_command(target_link_libraries cpkt_libssh2_shared PUBLIC cpkt::libssh2_shared)
cpkt_group_set_target_properties(cpkt_libssh2_shared PROPERTIES OUTPUT_NAME cpkt_libssh2 C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON SOVERSION "${CPKT_LIBSSH2_ABI_VERSION}" VERSION "${PROJECT_VERSION}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  cpkt_group_set_target_properties(cpkt_libssh2_shared PROPERTIES INSTALL_NAME_DIR "@rpath" BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path" BUILD_WITH_INSTALL_RPATH ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cpkt_group_set_target_properties(cpkt_libssh2_shared PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
endif()
set(CPKT_MQTTC_FACADE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mqttc")
set(CPKT_MQTTC_FACADE_INCLUDE_DIR "${CPKT_MQTTC_FACADE_GENERATED_DIR}/include")
set(CPKT_MQTTC_FACADE_HEADER "${CPKT_MQTTC_FACADE_INCLUDE_DIR}/cpkt/mqttc.h")
set(CPKT_MQTTC_FACADE_SOURCE "${CPKT_MQTTC_FACADE_GENERATED_DIR}/src/mqttc.c")
cpkt_group_get_target_property(CPKT_MQTTC_FACADE_NATIVE_INCLUDE_DIR
  cpkt::mqttc_static INTERFACE_INCLUDE_DIRECTORIES)
if(NOT CPKT_MQTTC_FACADE_NATIVE_INCLUDE_DIR)
  message(FATAL_ERROR "MQTT-C target does not expose public include directories")
endif()
set(CPKT_MQTTC_FACADE_NATIVE_HEADERS "${CPKT_MQTTC_FACADE_NATIVE_INCLUDE_DIR}/mqtt.h")
add_custom_command(
  OUTPUT "${CPKT_MQTTC_FACADE_HEADER}" "${CPKT_MQTTC_FACADE_SOURCE}"
  COMMAND "${CPKT_PYTHON3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tools/generate_mqttc_c89_facade.py"
    --include-dir "$<TARGET_PROPERTY:cpkt::mqttc_static,INTERFACE_INCLUDE_DIRECTORIES>"
    --header "${CPKT_MQTTC_FACADE_HEADER}"
    --source "${CPKT_MQTTC_FACADE_SOURCE}"
  DEPENDS
    "${CMAKE_SOURCE_DIR}/tools/generate_mqttc_c89_facade.py"
    ${CPKT_MQTTC_FACADE_NATIVE_HEADERS}
  VERBATIM)
set_source_files_properties("${CPKT_MQTTC_FACADE_SOURCE}" PROPERTIES GENERATED TRUE)
cpkt_group_add_custom_target(cpkt_mqttc_facade_generated
  DEPENDS "${CPKT_MQTTC_FACADE_HEADER}" "${CPKT_MQTTC_FACADE_SOURCE}")
if(TARGET cpkt_mqttc_project)
  cpkt_group_target_command(add_dependencies cpkt_mqttc_facade_generated cpkt_mqttc_project)
endif()
cpkt_group_add_library(cpkt_mqttc_static STATIC "${CPKT_MQTTC_FACADE_SOURCE}")
cpkt_group_target_command(add_dependencies cpkt_mqttc_static cpkt_mqttc_facade_generated)
cpkt_configure_c89_target(cpkt_mqttc_static)
cpkt_add_repo_warning_errors(cpkt_mqttc_static)
cpkt_group_target_command(target_compile_definitions cpkt_mqttc_static PRIVATE CPKT_MQTTC_STATIC)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  cpkt_group_target_command(target_compile_options cpkt_mqttc_static PRIVATE -Wno-comment)
endif()
cpkt_group_target_command(target_include_directories cpkt_mqttc_static PUBLIC
  "$<BUILD_INTERFACE:${CPKT_MQTTC_FACADE_INCLUDE_DIR}>")
cpkt_group_target_command(target_link_libraries cpkt_mqttc_static PUBLIC cpkt::mqttc_static)
cpkt_group_set_target_properties(cpkt_mqttc_static PROPERTIES OUTPUT_NAME cpkt_mqttc POSITION_INDEPENDENT_CODE ON)
cpkt_group_add_library(cpkt_mqttc_shared SHARED "${CPKT_MQTTC_FACADE_SOURCE}")
cpkt_group_target_command(add_dependencies cpkt_mqttc_shared cpkt_mqttc_facade_generated)
cpkt_configure_c89_target(cpkt_mqttc_shared)
cpkt_add_repo_warning_errors(cpkt_mqttc_shared)
cpkt_group_target_command(target_compile_definitions cpkt_mqttc_shared PRIVATE CPKT_MQTTC_BUILDING_SHARED)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  cpkt_group_target_command(target_compile_options cpkt_mqttc_shared PRIVATE -Wno-comment)
endif()
cpkt_group_target_command(target_include_directories cpkt_mqttc_shared PUBLIC
  "$<BUILD_INTERFACE:${CPKT_MQTTC_FACADE_INCLUDE_DIR}>")
cpkt_group_target_command(target_link_libraries cpkt_mqttc_shared PUBLIC cpkt::mqttc_shared)
cpkt_group_set_target_properties(cpkt_mqttc_shared PROPERTIES OUTPUT_NAME cpkt_mqttc C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON SOVERSION "${CPKT_MQTTC_ABI_VERSION}" VERSION "${PROJECT_VERSION}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  cpkt_group_set_target_properties(cpkt_mqttc_shared PROPERTIES INSTALL_NAME_DIR "@rpath" BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path" BUILD_WITH_INSTALL_RPATH ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cpkt_group_set_target_properties(cpkt_mqttc_shared PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
endif()
cpkt_group_add_library(cpkt_openssl_static STATIC src/openssl.c)
cpkt_configure_c89_target(cpkt_openssl_static)
cpkt_add_repo_warning_errors(cpkt_openssl_static)
cpkt_group_target_command(target_compile_definitions cpkt_openssl_static PRIVATE CPKT_OPENSSL_STATIC)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  cpkt_group_target_command(target_compile_options cpkt_openssl_static PRIVATE -Wno-deprecated-declarations)
endif()
cpkt_group_target_command(target_include_directories cpkt_openssl_static PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>")
cpkt_group_target_command(target_link_libraries cpkt_openssl_static PUBLIC
  cpkt::openssl_ssl_static
  cpkt::openssl_crypto_static)
cpkt_group_set_target_properties(cpkt_openssl_static PROPERTIES OUTPUT_NAME cpkt_openssl POSITION_INDEPENDENT_CODE ON)
cpkt_group_add_library(cpkt_openssl_shared SHARED src/openssl.c)
cpkt_configure_c89_target(cpkt_openssl_shared)
cpkt_add_repo_warning_errors(cpkt_openssl_shared)
cpkt_group_target_command(target_compile_definitions cpkt_openssl_shared PRIVATE CPKT_OPENSSL_BUILDING_SHARED)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  cpkt_group_target_command(target_compile_options cpkt_openssl_shared PRIVATE -Wno-deprecated-declarations)
endif()
cpkt_group_target_command(target_include_directories cpkt_openssl_shared PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>")
cpkt_group_target_command(target_link_libraries cpkt_openssl_shared PUBLIC
  cpkt::openssl_ssl_shared
  cpkt::openssl_crypto_shared)
cpkt_group_set_target_properties(cpkt_openssl_shared PROPERTIES OUTPUT_NAME cpkt_openssl C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON SOVERSION "${CPKT_OPENSSL_ABI_VERSION}" VERSION "${PROJECT_VERSION}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  cpkt_group_set_target_properties(cpkt_openssl_shared PROPERTIES INSTALL_NAME_DIR "@rpath" BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path" BUILD_WITH_INSTALL_RPATH ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cpkt_group_set_target_properties(cpkt_openssl_shared PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
endif()
set(CPKT_LUA_FACADE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/lua")
set(CPKT_LUA_FACADE_INCLUDE_DIR "${CPKT_LUA_FACADE_GENERATED_DIR}/include")
set(CPKT_LUA_FACADE_HEADER "${CPKT_LUA_FACADE_INCLUDE_DIR}/cpkt/lua.h")
set(CPKT_LUA_FACADE_SOURCE "${CPKT_LUA_FACADE_GENERATED_DIR}/src/lua.c")
cpkt_group_get_target_property(CPKT_LUA_FACADE_NATIVE_INCLUDE_DIR
  cpkt::lua_static INTERFACE_INCLUDE_DIRECTORIES)
if(NOT CPKT_LUA_FACADE_NATIVE_INCLUDE_DIR)
  message(FATAL_ERROR "Lua target does not expose public include directories")
endif()
set(CPKT_LUA_FACADE_NATIVE_HEADERS "${CPKT_LUA_FACADE_NATIVE_INCLUDE_DIR}/lua.h" "${CPKT_LUA_FACADE_NATIVE_INCLUDE_DIR}/luaconf.h" "${CPKT_LUA_FACADE_NATIVE_INCLUDE_DIR}/lauxlib.h" "${CPKT_LUA_FACADE_NATIVE_INCLUDE_DIR}/lualib.h")
add_custom_command(
  OUTPUT "${CPKT_LUA_FACADE_HEADER}" "${CPKT_LUA_FACADE_SOURCE}"
  COMMAND "${CPKT_PYTHON3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tools/generate_lua_c89_facade.py"
    --include-dir "$<TARGET_PROPERTY:cpkt::lua_static,INTERFACE_INCLUDE_DIRECTORIES>"
    --header "${CPKT_LUA_FACADE_HEADER}"
    --source "${CPKT_LUA_FACADE_SOURCE}"
  DEPENDS
    "${CMAKE_SOURCE_DIR}/tools/generate_lua_c89_facade.py"
    ${CPKT_LUA_FACADE_NATIVE_HEADERS}
  VERBATIM)
set_source_files_properties("${CPKT_LUA_FACADE_SOURCE}" PROPERTIES GENERATED TRUE)
cpkt_group_add_custom_target(cpkt_lua_facade_generated
  DEPENDS "${CPKT_LUA_FACADE_HEADER}" "${CPKT_LUA_FACADE_SOURCE}")
if(TARGET cpkt_lua_project)
  cpkt_group_target_command(add_dependencies cpkt_lua_facade_generated cpkt_lua_project)
endif()
cpkt_group_add_library(cpkt_lua_static STATIC "${CPKT_LUA_FACADE_SOURCE}")
cpkt_configure_c89_lua_native_header_target(cpkt_lua_static)
cpkt_group_target_command(add_dependencies cpkt_lua_static cpkt_lua_facade_generated)
cpkt_add_repo_warning_errors(cpkt_lua_static)
cpkt_group_target_command(target_compile_definitions cpkt_lua_static PRIVATE CPKT_LUA_STATIC)
cpkt_group_target_command(target_include_directories cpkt_lua_static PUBLIC
  "$<BUILD_INTERFACE:${CPKT_LUA_FACADE_INCLUDE_DIR}>")
cpkt_group_target_command(target_link_libraries cpkt_lua_static PUBLIC cpkt::lua_static)
cpkt_group_set_target_properties(cpkt_lua_static PROPERTIES OUTPUT_NAME cpkt_lua POSITION_INDEPENDENT_CODE ON)
cpkt_group_add_library(cpkt_lua_shared SHARED "${CPKT_LUA_FACADE_SOURCE}")
cpkt_configure_c89_lua_native_header_target(cpkt_lua_shared)
cpkt_group_target_command(add_dependencies cpkt_lua_shared cpkt_lua_facade_generated)
cpkt_add_repo_warning_errors(cpkt_lua_shared)
cpkt_group_target_command(target_compile_definitions cpkt_lua_shared PRIVATE CPKT_LUA_BUILDING_SHARED)
cpkt_group_target_command(target_include_directories cpkt_lua_shared PUBLIC
  "$<BUILD_INTERFACE:${CPKT_LUA_FACADE_INCLUDE_DIR}>")
cpkt_group_target_command(target_link_libraries cpkt_lua_shared PUBLIC cpkt::lua_shared)
cpkt_group_set_target_properties(cpkt_lua_shared PROPERTIES OUTPUT_NAME cpkt_lua C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON SOVERSION "${CPKT_LUA_ABI_VERSION}" VERSION "${PROJECT_VERSION}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  cpkt_group_set_target_properties(cpkt_lua_shared PROPERTIES INSTALL_NAME_DIR "@rpath" BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path" BUILD_WITH_INSTALL_RPATH ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cpkt_group_set_target_properties(cpkt_lua_shared PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
endif()
cpkt_group_add_library(cpkt_lua_runtime_static STATIC src/lua_runtime.c)
cpkt_configure_c89_lua_native_header_target(cpkt_lua_runtime_static)
cpkt_add_repo_warning_errors(cpkt_lua_runtime_static)
cpkt_group_target_command(target_include_directories cpkt_lua_runtime_static
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
)
cpkt_group_target_command(target_link_libraries cpkt_lua_runtime_static PUBLIC cpkt::lua_static)
cpkt_group_set_target_properties(cpkt_lua_runtime_static PROPERTIES OUTPUT_NAME cpkt_lua_runtime POSITION_INDEPENDENT_CODE ON)
cpkt_group_add_library(cpkt_lua_runtime_shared SHARED src/lua_runtime.c)
cpkt_configure_c89_lua_native_header_target(cpkt_lua_runtime_shared)
cpkt_add_repo_warning_errors(cpkt_lua_runtime_shared)
cpkt_group_target_command(target_include_directories cpkt_lua_runtime_shared
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
)
cpkt_group_target_command(target_link_libraries cpkt_lua_runtime_shared PUBLIC cpkt::lua_shared)
cpkt_group_set_target_properties(cpkt_lua_runtime_shared PROPERTIES OUTPUT_NAME cpkt_lua_runtime POSITION_INDEPENDENT_CODE ON SOVERSION "${CPKT_LUA_RUNTIME_ABI_VERSION}" VERSION "${PROJECT_VERSION}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  cpkt_group_set_target_properties(cpkt_lua_runtime_shared PROPERTIES INSTALL_NAME_DIR "@rpath" BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path" BUILD_WITH_INSTALL_RPATH ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cpkt_group_set_target_properties(cpkt_lua_runtime_shared PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
endif()
cpkt_group_add_library(cpkt_gssapi_static STATIC src/gssapi.c src/gssapi_ext.c
  src/gssapi_context_ext.c src/gssapi_krb5.c src/gssapi_cred_ext.c
  src/gssapi_mech_ext.c src/gssapi_oids.c src/gssapi_alloc.c)
cpkt_configure_c89_target(cpkt_gssapi_static)
cpkt_add_repo_warning_errors(cpkt_gssapi_static)
cpkt_group_target_command(target_include_directories cpkt_gssapi_static
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>")
cpkt_group_target_command(target_link_libraries cpkt_gssapi_static PUBLIC cpkt::gssapi_krb5_static)
cpkt_group_target_command(target_compile_definitions cpkt_gssapi_static PRIVATE CPKT_GSSAPI_FACADE_VERSION="${CPKT_GSSAPI_ABI_VERSION}")
cpkt_group_set_target_properties(cpkt_gssapi_static PROPERTIES OUTPUT_NAME cpkt_gssapi POSITION_INDEPENDENT_CODE ON)
cpkt_group_add_library(cpkt_gssapi_shared SHARED src/gssapi.c src/gssapi_ext.c
  src/gssapi_context_ext.c src/gssapi_krb5.c src/gssapi_cred_ext.c
  src/gssapi_mech_ext.c src/gssapi_oids.c src/gssapi_alloc.c)
cpkt_apply_auth_export_catalog(cpkt_gssapi_shared cpkt_gssapi)
cpkt_configure_c89_target(cpkt_gssapi_shared)
cpkt_add_repo_warning_errors(cpkt_gssapi_shared)
cpkt_group_target_command(target_include_directories cpkt_gssapi_shared
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>")
cpkt_group_target_command(target_link_libraries cpkt_gssapi_shared PUBLIC cpkt::gssapi_krb5_shared)
cpkt_group_target_command(target_link_libraries cpkt_gssapi_shared PRIVATE
  "${CPKT_EXTERNAL_ROOT}/krb5/install/lib/libkrb5${CMAKE_SHARED_LIBRARY_SUFFIX}")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cpkt_group_target_command(target_link_options cpkt_gssapi_shared PRIVATE "LINKER:--no-undefined")
endif()
cpkt_group_target_command(target_compile_definitions cpkt_gssapi_shared PRIVATE CPKT_GSSAPI_FACADE_VERSION="${CPKT_GSSAPI_ABI_VERSION}")
cpkt_group_set_target_properties(cpkt_gssapi_shared PROPERTIES OUTPUT_NAME cpkt_gssapi POSITION_INDEPENDENT_CODE ON SOVERSION "${CPKT_GSSAPI_ABI_VERSION}" VERSION "${PROJECT_VERSION}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  cpkt_group_set_target_properties(cpkt_gssapi_shared PROPERTIES INSTALL_NAME_DIR "@rpath" BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path" BUILD_WITH_INSTALL_RPATH ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cpkt_group_set_target_properties(cpkt_gssapi_shared PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
endif()
cpkt_group_add_library(cpkt_sasl_static STATIC src/sasl.c src/sasl_connection_properties.c src/sasl_util.c src/sasl_crypto.c
  src/sasl_plugin_utils.c src/sasl_plugin_aux.c src/sasl_plugin_canon.c
  src/sasl_plugin_params.c src/sasl_plugin_client.c
  src/sasl_plugin_server.c src/sasl_plugin_info.c)
cpkt_configure_c89_target(cpkt_sasl_static)
cpkt_add_repo_warning_errors(cpkt_sasl_static)
cpkt_group_target_command(target_include_directories cpkt_sasl_static
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
  PRIVATE
    "${CPKT_EXTERNAL_ROOT}/krb5/install/include")
cpkt_group_target_command(target_link_libraries cpkt_sasl_static PUBLIC cpkt::cyrus_sasl_static)
cpkt_group_target_command(target_link_libraries cpkt_sasl_static PRIVATE Threads::Threads)
cpkt_group_target_command(target_compile_definitions cpkt_sasl_static PRIVATE
  CPKT_SASL_FACADE_VERSION="${CPKT_SASL_ABI_VERSION}")
cpkt_group_set_target_properties(cpkt_sasl_static PROPERTIES OUTPUT_NAME cpkt_sasl POSITION_INDEPENDENT_CODE ON)
cpkt_group_add_library(cpkt_sasl_shared SHARED src/sasl.c src/sasl_connection_properties.c src/sasl_util.c src/sasl_crypto.c
  src/sasl_plugin_utils.c src/sasl_plugin_aux.c src/sasl_plugin_canon.c
  src/sasl_plugin_params.c src/sasl_plugin_client.c
  src/sasl_plugin_server.c src/sasl_plugin_info.c)
cpkt_apply_auth_export_catalog(cpkt_sasl_shared cpkt_sasl)
cpkt_configure_c89_target(cpkt_sasl_shared)
cpkt_add_repo_warning_errors(cpkt_sasl_shared)
cpkt_group_target_command(target_include_directories cpkt_sasl_shared
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
  PRIVATE
    "${CPKT_EXTERNAL_ROOT}/krb5/install/include")
cpkt_group_target_command(target_link_libraries cpkt_sasl_shared PUBLIC cpkt::cyrus_sasl_shared)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cpkt_group_target_command(target_link_options cpkt_sasl_shared PRIVATE "LINKER:--no-undefined")
endif()
cpkt_group_target_command(target_link_libraries cpkt_sasl_shared PRIVATE Threads::Threads)
cpkt_group_target_command(target_compile_definitions cpkt_sasl_shared PRIVATE
  CPKT_SASL_FACADE_VERSION="${CPKT_SASL_ABI_VERSION}")
cpkt_group_set_target_properties(cpkt_sasl_shared PROPERTIES OUTPUT_NAME cpkt_sasl POSITION_INDEPENDENT_CODE ON SOVERSION "${CPKT_SASL_ABI_VERSION}" VERSION "${PROJECT_VERSION}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  cpkt_group_set_target_properties(cpkt_sasl_shared PROPERTIES INSTALL_NAME_DIR "@rpath" BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path" BUILD_WITH_INSTALL_RPATH ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cpkt_group_set_target_properties(cpkt_sasl_shared PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH ON)
endif()
if(CPKT_BUILD_TESTS)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    cpkt_group_add_test(NAME sdk_runtime_loader
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/sdk_runtime_loader_test.py"
          --scratch "${CMAKE_BINARY_DIR}" --target "${CPKT_TARGET_ID}"
          --compiler "${CMAKE_C_COMPILER}" --readelf "${CMAKE_READELF}"
          --sysroot "${CMAKE_SYSROOT}")
    cpkt_group_set_tests_properties(sdk_runtime_loader PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
  endif()
  cpkt_group_add_test(NAME package_isolation_build
      COMMAND "${CPKT_OPERATION_PYTHON}" "${CMAKE_SOURCE_DIR}/tests/package_isolation_build_test.py")
  cpkt_group_add_test(NAME package_producer_reuse
      COMMAND "${CPKT_OPERATION_PYTHON}" "${CMAKE_SOURCE_DIR}/tests/package_producer_reuse_test.py")
  cpkt_group_add_test(NAME auth_package_discovery
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tests/auth_package_discovery_test.py" "${CMAKE_SOURCE_DIR}"
        --scratch "${CMAKE_BINARY_DIR}" --compiler "${CMAKE_C_COMPILER}"
        "--toolchain=${CMAKE_TOOLCHAIN_FILE}")
  cpkt_group_set_tests_properties(auth_package_discovery PROPERTIES LABELS "packaging;smoke" TIMEOUT 60)
  cpkt_group_add_test(NAME selected_test_executables
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tests/selected_test_executables_test.py"
        "${CMAKE_SOURCE_DIR}")
  cpkt_group_set_tests_properties(selected_test_executables PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
  include(cmake/CpktTestPslog.cmake)
  cpkt_group_add_test(NAME krb5_trace_prototypes
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/krb5_trace_prototypes_test.py"
        "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_C_COMPILER}")
  cpkt_group_set_tests_properties(krb5_trace_prototypes PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
  if(CPKT_SELECT_core OR CPKT_SELECT_misc)
    cpkt_add_test_pslog()
  endif()
  foreach(_cpkt_curl_linkage IN ITEMS static shared)
    cpkt_group_add_executable(cpkt_curl_async_dns_${_cpkt_curl_linkage}_test
          tests/curl_async_dns_test.c)
    cpkt_group_use_local_runtime(cpkt_curl_async_dns_${_cpkt_curl_linkage}_test)
    cpkt_group_target_command(target_compile_options cpkt_curl_async_dns_${_cpkt_curl_linkage}_test
          PRIVATE -std=c99)
    cpkt_add_repo_warning_errors(cpkt_curl_async_dns_${_cpkt_curl_linkage}_test)
    cpkt_group_target_command(target_link_libraries cpkt_curl_async_dns_${_cpkt_curl_linkage}_test PRIVATE
          cpkt::curl_${_cpkt_curl_linkage})
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND
            _cpkt_curl_linkage STREQUAL "shared")
      cpkt_group_set_target_properties(cpkt_curl_async_dns_shared_test PROPERTIES BUILD_RPATH "${CPKT_EXTERNAL_ROOT}/curl/install/lib;${CPKT_EXTERNAL_ROOT}/openssl/install/lib;${CPKT_EXTERNAL_ROOT}/nghttp2/install/lib;${CPKT_EXTERNAL_ROOT}/libssh2/install/lib;${CPKT_EXTERNAL_ROOT}/zlib/install/lib")
    endif()
  endforeach()
  foreach(_cpkt_ldap_linkage IN ITEMS static shared)
    cpkt_group_add_executable(cpkt_openldap_${_cpkt_ldap_linkage}_test
          tests/openldap_link_test.c)
    cpkt_group_use_local_runtime(cpkt_openldap_${_cpkt_ldap_linkage}_test)
    cpkt_configure_c89_target(cpkt_openldap_${_cpkt_ldap_linkage}_test)
    cpkt_add_repo_warning_errors(cpkt_openldap_${_cpkt_ldap_linkage}_test)
    cpkt_group_target_command(target_link_libraries cpkt_openldap_${_cpkt_ldap_linkage}_test PRIVATE
          cpkt::openldap_${_cpkt_ldap_linkage})
    cpkt_group_add_executable(cpkt_openldap_sasl_logging_${_cpkt_ldap_linkage}_test
          tests/openldap_sasl_logging_test.c)
    cpkt_group_use_local_runtime(cpkt_openldap_sasl_logging_${_cpkt_ldap_linkage}_test)
    cpkt_configure_c89_target(cpkt_openldap_sasl_logging_${_cpkt_ldap_linkage}_test)
    cpkt_add_repo_warning_errors(cpkt_openldap_sasl_logging_${_cpkt_ldap_linkage}_test)
    cpkt_group_target_command(target_link_libraries cpkt_openldap_sasl_logging_${_cpkt_ldap_linkage}_test
          PRIVATE cpkt::openldap_${_cpkt_ldap_linkage})
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND
            _cpkt_ldap_linkage STREQUAL "shared")
      cpkt_group_set_target_properties(cpkt_openldap_shared_test PROPERTIES BUILD_RPATH "${CPKT_EXTERNAL_ROOT}/openldap/install/lib;${CPKT_EXTERNAL_ROOT}/cyrus-sasl/install/lib;${CPKT_EXTERNAL_ROOT}/openssl/install/lib;${CPKT_EXTERNAL_ROOT}/krb5/install/lib")
      cpkt_group_set_target_properties(cpkt_openldap_sasl_logging_shared_test PROPERTIES BUILD_RPATH "${CPKT_EXTERNAL_ROOT}/openldap/install/lib;${CPKT_EXTERNAL_ROOT}/cyrus-sasl/install/lib;${CPKT_EXTERNAL_ROOT}/openssl/install/lib;${CPKT_EXTERNAL_ROOT}/krb5/install/lib")
    endif()
  endforeach()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    cpkt_group_add_executable(cpkt_openldap_sasl_default_logging_test
          tests/openldap_sasl_default_logging_test.c)
    cpkt_group_use_local_runtime(cpkt_openldap_sasl_default_logging_test)
    cpkt_configure_c89_target(cpkt_openldap_sasl_default_logging_test)
    cpkt_add_repo_warning_errors(cpkt_openldap_sasl_default_logging_test)
    cpkt_group_target_command(target_link_libraries cpkt_openldap_sasl_default_logging_test PRIVATE
          cpkt::openldap_static)
    cpkt_group_target_command(target_link_options cpkt_openldap_sasl_default_logging_test PRIVATE
          "-Wl,--wrap=sasl_client_new")
  endif()
  set(_cpkt_lua_runtime_example_file "${CMAKE_CURRENT_BINARY_DIR}/lua-runtime-c89-example-file.lua")
  set(_cpkt_lua_runtime_example_literal "${_cpkt_lua_runtime_example_file}")
  string(REPLACE "\\" "\\\\" _cpkt_lua_runtime_example_literal "${_cpkt_lua_runtime_example_literal}")
  string(REPLACE "'" "\\'" _cpkt_lua_runtime_example_literal "${_cpkt_lua_runtime_example_literal}")
  string(REPLACE "\n" "\\n" _cpkt_lua_runtime_example_literal "${_cpkt_lua_runtime_example_literal}")
  string(REPLACE "\r" "\\r" _cpkt_lua_runtime_example_literal "${_cpkt_lua_runtime_example_literal}")
  string(REPLACE "\t" "\\t" _cpkt_lua_runtime_example_literal "${_cpkt_lua_runtime_example_literal}")

  file(WRITE "${_cpkt_lua_runtime_example_file}"
      "local host = require('example_host')\n"
      "local chunk = require('example_chunk')\n"
      "if host.context ~= 'example-context' then error('bad file context') end\n"
      "if chunk.value ~= 'chunk' then error('bad file chunk') end\n"
      "if package.path ~= 'zero/?.lua;first/?.lua' then error('bad package path') end\n"
      "if package.cpath ~= 'zero/?.so;first/?.so' then error('bad package cpath') end\n"
      "if example_name ~= 'facade' then error('bad string global') end\n"
      "if example_flag ~= true then error('bad boolean global') end\n"
      "if example_count ~= 7 then error('bad integer global') end\n"
      "if example_number ~= 2.5 then error('bad number global') end\n"
      "if required_side_effect ~= 'ok' then error('bad require side effect') end\n"
      "if arg[0] ~= '${_cpkt_lua_runtime_example_literal}' then error('bad arg0') end\n"
      "if arg[1] ~= 'one' or arg[2] ~= 'two' then error('bad argv') end\n")
  cpkt_group_add_executable(cpkt_lua_runtime_c89_example
      examples/lua-runtime-c89/main.c
      examples/lua-runtime-c89/host_module.c)
  cpkt_group_use_local_runtime(cpkt_lua_runtime_c89_example)
  cpkt_add_repo_warning_errors(cpkt_lua_runtime_c89_example)
  set_source_files_properties(examples/lua-runtime-c89/main.c PROPERTIES COMPILE_OPTIONS "-std=c89")
  set_source_files_properties(examples/lua-runtime-c89/host_module.c PROPERTIES COMPILE_OPTIONS "-std=c99")
  cpkt_group_target_command(target_link_libraries cpkt_lua_runtime_c89_example PRIVATE cpkt_lua_runtime_static)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CPKT_TARGET_LIBC STREQUAL "musl")
  else()
    file(REMOVE
          "${CMAKE_CURRENT_BINARY_DIR}/cpkt_audio_live_vox_static_c89_example${CMAKE_EXECUTABLE_SUFFIX}")
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CPKT_TARGET_LIBC STREQUAL "musl")
  else()
    file(REMOVE
          "${CMAKE_CURRENT_BINARY_DIR}/cpkt_sus_live_vox_static_c89_example${CMAKE_EXECUTABLE_SUFFIX}")
  endif()
  cpkt_group_add_executable(cpkt_mqttc_smoke_shared examples/mqttc_smoke.c)
  cpkt_group_use_local_runtime(cpkt_mqttc_smoke_shared)
  cpkt_group_target_command(target_compile_options cpkt_mqttc_smoke_shared PRIVATE -std=c99)
  cpkt_group_target_command(target_link_libraries cpkt_mqttc_smoke_shared PRIVATE cpkt::mqttc_shared)
  cpkt_group_add_executable(cpkt_mqttc_smoke_static examples/mqttc_smoke.c)
  cpkt_group_use_local_runtime(cpkt_mqttc_smoke_static)
  cpkt_group_target_command(target_compile_options cpkt_mqttc_smoke_static PRIVATE -std=c99)
  cpkt_group_target_command(target_link_libraries cpkt_mqttc_smoke_static PRIVATE cpkt::mqttc_static)



  if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
    foreach(_cpkt_vfs_case IN ITEMS borrowed-file provider-identity)
      string(REPLACE "-" "_" _cpkt_vfs_test_name "${_cpkt_vfs_case}")
    endforeach()
  endif()
  foreach(_cpkt_bio_linkage IN ITEMS static shared)
    cpkt_group_add_executable(cpkt_openssl_bio_lifecycle_${_cpkt_bio_linkage}_test
          tests/openssl_bio_lifecycle_test.c)
    cpkt_group_use_local_runtime(cpkt_openssl_bio_lifecycle_${_cpkt_bio_linkage}_test
          cpkt::openssl_ssl_shared cpkt::openssl_crypto_shared)
    cpkt_group_target_command(target_compile_options cpkt_openssl_bio_lifecycle_${_cpkt_bio_linkage}_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_openssl_bio_lifecycle_${_cpkt_bio_linkage}_test)
    cpkt_group_target_command(target_link_libraries cpkt_openssl_bio_lifecycle_${_cpkt_bio_linkage}_test PRIVATE
          cpkt_openssl_${_cpkt_bio_linkage})
  endforeach()
  foreach(_cpkt_record_linkage IN ITEMS static shared)
    cpkt_group_add_executable(cpkt_openssl_record_boundary_${_cpkt_record_linkage}_test
          tests/openssl_record_boundary_test.c)
    cpkt_group_use_local_runtime(cpkt_openssl_record_boundary_${_cpkt_record_linkage}_test
          cpkt::openssl_ssl_shared cpkt::openssl_crypto_shared)
    cpkt_configure_c89_target(cpkt_openssl_record_boundary_${_cpkt_record_linkage}_test)
    cpkt_add_repo_warning_errors(cpkt_openssl_record_boundary_${_cpkt_record_linkage}_test)
    cpkt_group_target_command(target_link_libraries cpkt_openssl_record_boundary_${_cpkt_record_linkage}_test PRIVATE
          cpkt_openssl_${_cpkt_record_linkage})
    if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
      foreach(_cpkt_record_case IN ITEMS poll_empty poll_timeout poll_records
                poll_invalid mmsg_empty mmsg_invalid mmsg_callbacks)
        cpkt_group_add_test(NAME openssl_${_cpkt_record_case}_${_cpkt_record_linkage}
                  COMMAND cpkt_openssl_record_boundary_${_cpkt_record_linkage}_test
                    ${_cpkt_record_case})
        cpkt_group_set_tests_properties(openssl_${_cpkt_record_case}_${_cpkt_record_linkage} PROPERTIES LABELS "facade;memcheck" TIMEOUT 15)
      endforeach()
    endif()
  endforeach()
  cpkt_group_add_executable(cpkt_lua_facade_test tests/lua_facade_test.c)
  cpkt_group_use_local_runtime(cpkt_lua_facade_test)
  cpkt_configure_c89_target(cpkt_lua_facade_test)
  cpkt_add_repo_warning_errors(cpkt_lua_facade_test)
  cpkt_group_target_command(target_link_libraries cpkt_lua_facade_test PRIVATE cpkt_lua_static)
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    cpkt_group_add_test(NAME darwin_bio_runtime_paths
          COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/darwin_test_runtime_paths_test.py"
            "${CMAKE_BINARY_DIR}" "${CMAKE_MAKE_PROGRAM}"
            cpkt_openssl_bio_lifecycle_shared_test
            "$<TARGET_FILE_DIR:cpkt::openssl_ssl_shared>"
            "$<TARGET_FILE_DIR:cpkt::openssl_crypto_shared>")
    cpkt_group_set_tests_properties(darwin_bio_runtime_paths PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
  else()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      set(_cpkt_postgresql_oauth_staging_paths)
      foreach(_cpkt_postgresql_oauth_dependency IN ITEMS
                cpkt::curl_shared cpkt::zlib_shared cpkt::openssl_ssl_shared
                cpkt::libssh2_shared cpkt::nghttp2_shared cpkt::gssapi_krb5_shared
                cpkt::openldap_shared cpkt::cyrus_sasl_shared)
        cpkt_group_get_target_property(_cpkt_postgresql_oauth_dependency_location
                  ${_cpkt_postgresql_oauth_dependency} IMPORTED_LOCATION)
        get_filename_component(_cpkt_postgresql_oauth_dependency_dir
                  "${_cpkt_postgresql_oauth_dependency_location}" DIRECTORY)
        list(APPEND _cpkt_postgresql_oauth_staging_paths
                  "${_cpkt_postgresql_oauth_dependency_dir}")
      endforeach()
      list(JOIN _cpkt_postgresql_oauth_staging_paths ":"
              _cpkt_postgresql_oauth_staging_rpath)
    endif()
    cpkt_group_add_executable(cpkt_gssapi_facade_test tests/gssapi_facade_test.c)
    cpkt_group_use_local_runtime(cpkt_gssapi_facade_test)
    cpkt_group_target_command(target_compile_options cpkt_gssapi_facade_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_gssapi_facade_test)
    cpkt_group_target_command(target_link_libraries cpkt_gssapi_facade_test PRIVATE cpkt_gssapi_static)
    cpkt_group_add_executable(cpkt_gssapi_logging_test tests/gssapi_logging_test.c)
    cpkt_group_use_local_runtime(cpkt_gssapi_logging_test)
    cpkt_group_target_command(target_compile_options cpkt_gssapi_logging_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_gssapi_logging_test)
    cpkt_group_target_command(target_link_libraries cpkt_gssapi_logging_test PRIVATE cpkt_gssapi_static)
    cpkt_group_add_executable(cpkt_gssapi_extended_test
          tests/gssapi_extended_test.c)
    cpkt_group_use_local_runtime(cpkt_gssapi_extended_test)
    cpkt_group_target_command(target_compile_options cpkt_gssapi_extended_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_gssapi_extended_test)
    cpkt_group_target_command(target_link_libraries cpkt_gssapi_extended_test PRIVATE cpkt_gssapi_static)
    cpkt_group_add_executable(cpkt_gssapi_lucid_conversion_test
          tests/gssapi_lucid_conversion_test.c src/gssapi_ext.c)
    cpkt_group_use_local_runtime(cpkt_gssapi_lucid_conversion_test)
    cpkt_group_target_command(target_compile_options cpkt_gssapi_lucid_conversion_test PRIVATE -std=c99)
    cpkt_add_repo_warning_errors(cpkt_gssapi_lucid_conversion_test)
    cpkt_group_target_command(target_include_directories cpkt_gssapi_lucid_conversion_test PRIVATE
          "${CMAKE_SOURCE_DIR}/include")
    cpkt_group_target_command(target_link_libraries cpkt_gssapi_lucid_conversion_test PRIVATE
          cpkt::gssapi_krb5_shared)
    cpkt_group_add_executable(cpkt_gssapi_iov_native_peer_test
          tests/gssapi_iov_native_peer_test.c src/gssapi_ext.c)
    cpkt_group_use_local_runtime(cpkt_gssapi_iov_native_peer_test)
    cpkt_group_target_command(target_compile_options cpkt_gssapi_iov_native_peer_test PRIVATE -std=c99)
    cpkt_add_repo_warning_errors(cpkt_gssapi_iov_native_peer_test)
    cpkt_group_target_command(target_include_directories cpkt_gssapi_iov_native_peer_test PRIVATE
          "${CMAKE_SOURCE_DIR}/include")
    cpkt_group_target_command(target_link_libraries cpkt_gssapi_iov_native_peer_test PRIVATE
          cpkt::gssapi_krb5_shared)
    cpkt_group_add_executable(cpkt_gssapi_oid_native_parity_test
          tests/gssapi_oid_native_parity_test.c)
    cpkt_group_use_local_runtime(cpkt_gssapi_oid_native_parity_test)
    cpkt_group_target_command(target_compile_options cpkt_gssapi_oid_native_parity_test PRIVATE -std=c99)
    cpkt_add_repo_warning_errors(cpkt_gssapi_oid_native_parity_test)
    cpkt_group_target_command(target_link_libraries cpkt_gssapi_oid_native_parity_test PRIVATE
          cpkt_gssapi_static)
    cpkt_group_add_executable(cpkt_sasl_plugin_facade_test
          tests/sasl_plugin_facade_test.c)
    cpkt_group_use_local_runtime(cpkt_sasl_plugin_facade_test)
    cpkt_group_target_command(target_compile_options cpkt_sasl_plugin_facade_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sasl_plugin_facade_test)
    cpkt_group_target_command(target_link_libraries cpkt_sasl_plugin_facade_test PRIVATE cpkt_sasl_static)
    cpkt_group_add_executable(cpkt_sasl_property_lifetime_test
          tests/sasl_property_lifetime_test.c)
    cpkt_group_use_local_runtime(cpkt_sasl_property_lifetime_test)
    cpkt_group_target_command(target_compile_options cpkt_sasl_property_lifetime_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sasl_property_lifetime_test)
    cpkt_group_target_command(target_link_libraries cpkt_sasl_property_lifetime_test PRIVATE cpkt_sasl_static)
    cpkt_group_add_executable(cpkt_sasl_property_coherence_test
          tests/sasl_property_coherence_test.c)
    cpkt_group_use_local_runtime(cpkt_sasl_property_coherence_test)
    cpkt_configure_c89_target(cpkt_sasl_property_coherence_test)
    cpkt_add_repo_warning_errors(cpkt_sasl_property_coherence_test)
    cpkt_group_target_command(target_link_libraries cpkt_sasl_property_coherence_test PRIVATE
          cpkt_sasl_static)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      cpkt_group_target_command(target_link_options cpkt_sasl_property_coherence_test PRIVATE
              "-Wl,--wrap=calloc")
    endif()
    cpkt_group_add_executable(cpkt_sasl_connection_property_test
          tests/sasl_connection_property_test.c)
    cpkt_group_use_local_runtime(cpkt_sasl_connection_property_test)
    cpkt_group_target_command(target_compile_options cpkt_sasl_connection_property_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sasl_connection_property_test)
    cpkt_group_target_command(target_link_libraries cpkt_sasl_connection_property_test PRIVATE
          cpkt_sasl_static)
    cpkt_group_add_executable(cpkt_sasl_idle_registration_test
          tests/sasl_idle_registration_test.c)
    cpkt_group_use_local_runtime(cpkt_sasl_idle_registration_test)
    cpkt_group_target_command(target_compile_options cpkt_sasl_idle_registration_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sasl_idle_registration_test)
    cpkt_group_target_command(target_link_libraries cpkt_sasl_idle_registration_test PRIVATE cpkt_sasl_static)
    cpkt_group_add_executable(cpkt_sasl_registration_failure_test
          tests/sasl_registration_failure_test.c)
    cpkt_group_use_local_runtime(cpkt_sasl_registration_failure_test)
    cpkt_group_target_command(target_compile_options cpkt_sasl_registration_failure_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sasl_registration_failure_test)
    cpkt_group_target_command(target_link_libraries cpkt_sasl_registration_failure_test PRIVATE
          cpkt_sasl_static)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      cpkt_group_add_executable(cpkt_sasl_utility_allocation_failure_test
              tests/sasl_utility_allocation_failure_test.c)
      cpkt_group_use_local_runtime(cpkt_sasl_utility_allocation_failure_test)
      cpkt_group_target_command(target_compile_options cpkt_sasl_utility_allocation_failure_test PRIVATE -std=c89)
      cpkt_add_repo_warning_errors(cpkt_sasl_utility_allocation_failure_test)
      cpkt_group_target_command(target_link_libraries cpkt_sasl_utility_allocation_failure_test PRIVATE
              cpkt_sasl_static)
      cpkt_group_target_command(target_link_options cpkt_sasl_utility_allocation_failure_test PRIVATE
              "-Wl,--wrap=calloc")
    endif()
    cpkt_group_add_executable(cpkt_krb5_error_format_test tests/krb5_error_format_test.c)
    cpkt_group_use_local_runtime(cpkt_krb5_error_format_test)
    cpkt_group_target_command(target_compile_options cpkt_krb5_error_format_test PRIVATE -std=c99)
    cpkt_add_repo_warning_errors(cpkt_krb5_error_format_test)
    cpkt_group_target_command(target_link_libraries cpkt_krb5_error_format_test PRIVATE
          cpkt::gssapi_krb5_static)
    cpkt_group_add_executable(cpkt_openssl_facade_test tests/openssl_facade_test.c)
    cpkt_group_use_local_runtime(cpkt_openssl_facade_test)
    cpkt_group_target_command(target_compile_options cpkt_openssl_facade_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_openssl_facade_test)
    cpkt_group_target_command(target_link_libraries cpkt_openssl_facade_test PRIVATE cpkt_openssl_static)
    cpkt_group_add_executable(cpkt_nghttp2_facade_test tests/nghttp2_facade_test.c)
    cpkt_group_use_local_runtime(cpkt_nghttp2_facade_test)
    cpkt_group_target_command(target_compile_options cpkt_nghttp2_facade_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_nghttp2_facade_test)
    cpkt_group_target_command(target_link_libraries cpkt_nghttp2_facade_test PRIVATE cpkt_nghttp2_static)
    cpkt_group_add_executable(cpkt_libssh2_header_facade_test
          tests/libssh2_header_facade_test.c)
    cpkt_group_target_command(add_dependencies cpkt_libssh2_header_facade_test
          cpkt_libssh2_facade_generated)
    cpkt_group_use_local_runtime(cpkt_libssh2_header_facade_test)
    cpkt_group_target_command(target_compile_options cpkt_libssh2_header_facade_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_libssh2_header_facade_test)
    cpkt_group_target_command(target_include_directories cpkt_libssh2_header_facade_test PRIVATE
          "${CPKT_LIBSSH2_FACADE_INCLUDE_DIR}")
    cpkt_group_add_executable(cpkt_libssh2_facade_test tests/libssh2_facade_test.c)
    cpkt_group_use_local_runtime(cpkt_libssh2_facade_test)
    cpkt_group_target_command(target_compile_options cpkt_libssh2_facade_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_libssh2_facade_test)
    cpkt_group_target_command(target_link_libraries cpkt_libssh2_facade_test PRIVATE cpkt_libssh2_static)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      cpkt_group_add_executable(cpkt_libssh2_sftp_eof_test
              tests/libssh2_sftp_eof_test.c)
      cpkt_group_use_local_runtime(cpkt_libssh2_sftp_eof_test)
      cpkt_group_target_command(target_compile_options cpkt_libssh2_sftp_eof_test PRIVATE -std=c99)
      cpkt_group_target_command(target_link_options cpkt_libssh2_sftp_eof_test PRIVATE
              "-Wl,--wrap=libssh2_sftp_readdir_ex")
      cpkt_add_repo_warning_errors(cpkt_libssh2_sftp_eof_test)
      cpkt_group_target_command(target_link_libraries cpkt_libssh2_sftp_eof_test PRIVATE
              cpkt_libssh2_static cpkt::libssh2_static)
      cpkt_group_add_executable(cpkt_libssh2_poll_elapsed_test
              tests/libssh2_poll_elapsed_test.c)
      cpkt_group_use_local_runtime(cpkt_libssh2_poll_elapsed_test)
      cpkt_group_target_command(target_compile_options cpkt_libssh2_poll_elapsed_test PRIVATE -std=c99)
      cpkt_group_target_command(target_link_options cpkt_libssh2_poll_elapsed_test PRIVATE
              "-Wl,--wrap=poll")
      if(CPKT_TARGET_ID STREQUAL "armhf-linux-musl")
        cpkt_group_target_command(target_compile_definitions cpkt_libssh2_poll_elapsed_test PRIVATE
                  CPKT_LIBSSH2_TEST_MUSL_TIME64=1)
        cpkt_group_target_command(target_link_options cpkt_libssh2_poll_elapsed_test PRIVATE
                  "-Wl,--wrap=__gettimeofday_time64")
      else()
        cpkt_group_target_command(target_link_options cpkt_libssh2_poll_elapsed_test PRIVATE
                  "-Wl,--wrap=gettimeofday")
      endif()
      cpkt_add_repo_warning_errors(cpkt_libssh2_poll_elapsed_test)
      cpkt_group_target_command(target_link_libraries cpkt_libssh2_poll_elapsed_test PRIVATE
              cpkt::libssh2_static)
    endif()
    cpkt_group_add_executable(cpkt_mqttc_header_facade_test
          tests/mqttc_header_facade_test.c)
    cpkt_group_target_command(add_dependencies cpkt_mqttc_header_facade_test cpkt_mqttc_facade_generated)
    cpkt_group_use_local_runtime(cpkt_mqttc_header_facade_test)
    cpkt_group_target_command(target_compile_options cpkt_mqttc_header_facade_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_mqttc_header_facade_test)
    cpkt_group_target_command(target_include_directories cpkt_mqttc_header_facade_test PRIVATE
          "${CPKT_MQTTC_FACADE_INCLUDE_DIR}")
    cpkt_group_add_executable(cpkt_mqttc_facade_test tests/mqttc_facade_test.c)
    cpkt_group_use_local_runtime(cpkt_mqttc_facade_test)
    cpkt_group_target_command(target_compile_options cpkt_mqttc_facade_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_mqttc_facade_test)
    cpkt_group_target_command(target_link_libraries cpkt_mqttc_facade_test PRIVATE cpkt_mqttc_static)
    cpkt_group_add_executable(cpkt_sasl_facade_test tests/sasl_facade_test.c)
    cpkt_group_use_local_runtime(cpkt_sasl_facade_test)
    cpkt_group_target_command(target_compile_options cpkt_sasl_facade_test PRIVATE -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sasl_facade_test)
    cpkt_group_target_command(target_link_libraries cpkt_sasl_facade_test PRIVATE cpkt_sasl_static)
    cpkt_group_add_executable(cpkt_sasl_privileged_plugin_path_test
          tests/sasl_privileged_plugin_path_test.c)
    cpkt_group_use_local_runtime(cpkt_sasl_privileged_plugin_path_test)
    cpkt_group_target_command(target_compile_options cpkt_sasl_privileged_plugin_path_test PRIVATE
          -std=c89)
    cpkt_add_repo_warning_errors(cpkt_sasl_privileged_plugin_path_test)
    cpkt_group_target_command(target_link_libraries cpkt_sasl_privileged_plugin_path_test PRIVATE
          cpkt::cyrus_sasl_shared cpkt::gssapi_krb5_shared)
    cpkt_group_add_executable(cpkt_sasl_callback_retry_test
          tests/sasl_callback_retry_test.c
          src/sasl.c src/sasl_connection_properties.c src/sasl_util.c)
    if(TARGET cpkt_cyrus_sasl_project)
      cpkt_group_target_command(add_dependencies cpkt_sasl_callback_retry_test cpkt_cyrus_sasl_project)
    endif()
    cpkt_group_use_local_runtime(cpkt_sasl_callback_retry_test)
    cpkt_group_get_target_property(cpkt_sasl_native_include_dir cpkt::cyrus_sasl_static
          INTERFACE_INCLUDE_DIRECTORIES)
    cpkt_group_target_command(target_include_directories cpkt_sasl_callback_retry_test PRIVATE
          "${CMAKE_SOURCE_DIR}/include"
          "${cpkt_sasl_native_include_dir}"
          "${CPKT_EXTERNAL_ROOT}/krb5/install/include")
    cpkt_group_target_command(target_compile_options cpkt_sasl_callback_retry_test PRIVATE -std=c89)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      cpkt_group_target_command(target_compile_options cpkt_sasl_callback_retry_test PRIVATE
              -ffunction-sections -fdata-sections)
      cpkt_group_target_command(target_link_options cpkt_sasl_callback_retry_test PRIVATE
              -Wl,--gc-sections)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
      cpkt_group_target_command(target_compile_options cpkt_sasl_callback_retry_test PRIVATE
              -ffunction-sections -fdata-sections)
      cpkt_group_target_command(target_link_options cpkt_sasl_callback_retry_test PRIVATE
              -Wl,-dead_strip)
    endif()
    cpkt_add_repo_warning_errors(cpkt_sasl_callback_retry_test)
    cpkt_group_target_command(target_link_libraries cpkt_sasl_callback_retry_test PRIVATE
          cpkt::cyrus_sasl_shared)
    cpkt_add_lua_runtime_mock_test(cpkt_lua_runtime_mock_test tests/lua_runtime_mock_test.c)
  endif()
  cpkt_group_add_executable(cpkt_lua_runtime_test tests/lua_runtime_test.c)
  cpkt_group_use_local_runtime(cpkt_lua_runtime_test)
  cpkt_group_target_command(target_compile_options cpkt_lua_runtime_test PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(cpkt_lua_runtime_test)
  cpkt_group_target_command(target_link_libraries cpkt_lua_runtime_test PRIVATE
        cpkt_lua_runtime_static
        cpkt::cmocka
      )
  foreach(_cpkt_sasl_roundtrip_linkage IN ITEMS static shared)
    cpkt_group_add_executable(cpkt_sasl_plugin_roundtrip_${_cpkt_sasl_roundtrip_linkage}_test
          tests/sasl_plugin_roundtrip_test.c)
    cpkt_configure_c89_target(cpkt_sasl_plugin_roundtrip_${_cpkt_sasl_roundtrip_linkage}_test)
    cpkt_add_repo_warning_errors(cpkt_sasl_plugin_roundtrip_${_cpkt_sasl_roundtrip_linkage}_test)
    cpkt_group_use_local_runtime(cpkt_sasl_plugin_roundtrip_${_cpkt_sasl_roundtrip_linkage}_test)
    cpkt_group_target_command(target_link_libraries
          cpkt_sasl_plugin_roundtrip_${_cpkt_sasl_roundtrip_linkage}_test PRIVATE
          cpkt_sasl_${_cpkt_sasl_roundtrip_linkage})
    if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
      foreach(_cpkt_sasl_roundtrip_case IN ITEMS global_options security_callbacks)
        cpkt_group_add_test(NAME sasl_plugin_roundtrip_${_cpkt_sasl_roundtrip_linkage}_${_cpkt_sasl_roundtrip_case}
              COMMAND cpkt_sasl_plugin_roundtrip_${_cpkt_sasl_roundtrip_linkage}_test
                ${_cpkt_sasl_roundtrip_case})
        cpkt_group_set_tests_properties(sasl_plugin_roundtrip_${_cpkt_sasl_roundtrip_linkage}_${_cpkt_sasl_roundtrip_case}
              PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
      endforeach()
    endif()
  endforeach()
  foreach(_cpkt_lua_policy_linkage IN ITEMS static shared)
    cpkt_group_add_executable(cpkt_lua_runtime_contract_${_cpkt_lua_policy_linkage}_test
          tests/lua_runtime_contract_test.c)
    cpkt_configure_c89_target(cpkt_lua_runtime_contract_${_cpkt_lua_policy_linkage}_test)
    cpkt_add_repo_warning_errors(cpkt_lua_runtime_contract_${_cpkt_lua_policy_linkage}_test)
    cpkt_group_use_local_runtime(cpkt_lua_runtime_contract_${_cpkt_lua_policy_linkage}_test)
    cpkt_group_target_command(target_link_libraries
          cpkt_lua_runtime_contract_${_cpkt_lua_policy_linkage}_test PRIVATE
          cpkt_lua_runtime_${_cpkt_lua_policy_linkage}
          cpkt_lua_${_cpkt_lua_policy_linkage})
    if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
      foreach(_cpkt_lua_policy_case IN ITEMS large_arguments large_results large_yields
              closed_error close_error_replaced protected_limit xprotected_limit
              xhandler_limit xhandler_error_limit xhandler_normal gc_limit gc_coroutine_limit gc_finite gc_close_limit gc_close_after_limit native_gc_hooks
              coroutine_close_existing_limit coroutine_close_reopen_limit coroutine_close_cleared_limit coroutine_close_normal_results
              registry_limit protected_yields yielded_protected_limit upvalue_limit existing_wrap_limit
              native_upvalues shared_resume_budget shared_wrap_budget error_conversion_oom numeric_error preload_registration preload_arguments
              preload_lua_upvalues preload_c_upvalues preload_c_allocation reopen_resume_limit reopen_wrap_limit reopen_library_identity construction_failure)
        cpkt_group_add_test(NAME lua_runtime_contract_${_cpkt_lua_policy_linkage}_${_cpkt_lua_policy_case}
              COMMAND cpkt_lua_runtime_contract_${_cpkt_lua_policy_linkage}_test ${_cpkt_lua_policy_case})
        cpkt_group_set_tests_properties(lua_runtime_contract_${_cpkt_lua_policy_linkage}_${_cpkt_lua_policy_case}
              PROPERTIES LABELS "facade;memcheck" TIMEOUT 10)
      endforeach()
    endif()
  endforeach()

  if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
    cpkt_group_add_test(NAME lua_runtime_unit COMMAND cpkt_lua_runtime_test)
    cpkt_group_set_tests_properties(lua_runtime_unit PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
    foreach(_cpkt_ldap_linkage IN ITEMS static shared)
      cpkt_group_add_test(NAME openldap_${_cpkt_ldap_linkage}
              COMMAND cpkt_openldap_${_cpkt_ldap_linkage}_test)
      cpkt_group_add_test(NAME openldap_sasl_logging_${_cpkt_ldap_linkage}
              COMMAND cpkt_openldap_sasl_logging_${_cpkt_ldap_linkage}_test)
      cpkt_group_set_tests_properties(openldap_sasl_logging_${_cpkt_ldap_linkage} PROPERTIES LABELS "integration;memcheck" TIMEOUT 15)
      cpkt_group_set_tests_properties(openldap_${_cpkt_ldap_linkage} PROPERTIES LABELS "integration;smoke" TIMEOUT 10)
    endforeach()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      foreach(_cpkt_ldap_sink IN ITEMS default custom)
        cpkt_group_add_test(NAME openldap_sasl_${_cpkt_ldap_sink}_sink
                  COMMAND cpkt_openldap_sasl_default_logging_test ${_cpkt_ldap_sink})
        cpkt_group_set_tests_properties(openldap_sasl_${_cpkt_ldap_sink}_sink PROPERTIES LABELS "integration;memcheck" TIMEOUT 15)
      endforeach()
    endif()
    cpkt_group_add_test(NAME curl_server_bind
          COMMAND "${CPKT_PYTHON3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/curl_async_dns_server_test.py")
    cpkt_group_set_tests_properties(curl_server_bind PROPERTIES LABELS "integration;smoke" TIMEOUT 10)
    set(_cpkt_curl_harness_runner)
    foreach(_cpkt_runner_argument IN LISTS CPKT_TEST_EXECUTABLE_PREFIX)
      list(APPEND _cpkt_curl_harness_runner "--runner=${_cpkt_runner_argument}")
    endforeach()
    foreach(_cpkt_curl_linkage IN ITEMS static shared)
      cpkt_group_add_test(NAME curl_async_dns_${_cpkt_curl_linkage}
              COMMAND cpkt_curl_async_dns_${_cpkt_curl_linkage}_test)
      cpkt_group_add_test(NAME curl_multi_socket_${_cpkt_curl_linkage}
              COMMAND "${CPKT_PYTHON3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/curl_async_dns_server.py"
                ${_cpkt_curl_harness_runner}
                "$<TARGET_FILE:cpkt_curl_async_dns_${_cpkt_curl_linkage}_test>")
      cpkt_group_set_tests_properties(curl_async_dns_${_cpkt_curl_linkage} curl_multi_socket_${_cpkt_curl_linkage} PROPERTIES LABELS "integration;smoke" TIMEOUT 30)
    endforeach()
    cpkt_group_add_test(
          NAME patch_series_portability
          COMMAND "${CMAKE_COMMAND}"
            "-DCPKT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DCPKT_PATCH_TEST_ROOT=${CMAKE_BINARY_DIR}"
            -P "${CMAKE_SOURCE_DIR}/tests/patch_series_portability_test.cmake")
    cpkt_group_add_test(
          NAME static_archive_member_cleanup
          COMMAND "${CMAKE_COMMAND}"
            "-DCPKT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DCPKT_TEST_BINARY_DIR=${CMAKE_BINARY_DIR}"
            "-DCPKT_TEST_CC=${CMAKE_C_COMPILER}"
            "-DCPKT_TEST_AR=${CMAKE_AR}"
            "-DCPKT_TEST_RANLIB=${CMAKE_RANLIB}"
            "-DCPKT_TEST_EXECUTABLE_PREFIX=${CPKT_TEST_EXECUTABLE_PREFIX}"
            "-DCPKT_TEST_RUNTIME_LINK_OPTIONS=${CPKT_LOCAL_RUNTIME_LINK_OPTIONS}"
            "-DCPKT_TEST_RUNTIME_LOADER=${CPKT_RUNTIME_LOADER}"
            -P "${CMAKE_SOURCE_DIR}/tests/static_archive_member_cleanup_test.cmake")
    cpkt_group_set_tests_properties(patch_series_portability static_archive_member_cleanup PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
    cpkt_group_add_test(NAME lua_facade_unit COMMAND cpkt_lua_facade_test)
    foreach(_cpkt_bio_linkage IN ITEMS static shared)
      cpkt_group_add_test(NAME openssl_bio_lifecycle_${_cpkt_bio_linkage}
              COMMAND cpkt_openssl_bio_lifecycle_${_cpkt_bio_linkage}_test)
      cpkt_group_set_tests_properties(openssl_bio_lifecycle_${_cpkt_bio_linkage} PROPERTIES LABELS "facade;memcheck" TIMEOUT 15)
    endforeach()
    cpkt_group_add_test(
          NAME lua_runtime_c89_example
          COMMAND cpkt_lua_runtime_c89_example "${_cpkt_lua_runtime_example_file}")
    cpkt_group_add_test(NAME mqttc_example_shared COMMAND cpkt_mqttc_smoke_shared)
    cpkt_group_add_test(NAME mqttc_example_static COMMAND cpkt_mqttc_smoke_static)

    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    elseif(NOT CPKT_NATIVE_BOOTLIN_RUNTIME)
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
      cpkt_group_add_test(NAME openssl_facade COMMAND cpkt_openssl_facade_test)
      cpkt_group_add_test(NAME nghttp2_facade COMMAND cpkt_nghttp2_facade_test)
      cpkt_group_add_test(NAME libssh2_header_facade COMMAND cpkt_libssh2_header_facade_test)
      cpkt_group_add_test(NAME libssh2_facade COMMAND cpkt_libssh2_facade_test)
      if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        cpkt_group_add_test(NAME libssh2_sftp_eof COMMAND cpkt_libssh2_sftp_eof_test)
        cpkt_group_add_test(NAME libssh2_poll_elapsed COMMAND cpkt_libssh2_poll_elapsed_test)
      endif()
      cpkt_group_add_test(NAME mqttc_header_facade COMMAND cpkt_mqttc_header_facade_test)
      cpkt_group_add_test(NAME mqttc_facade COMMAND cpkt_mqttc_facade_test)
      cpkt_group_add_test(NAME openssl_builddata
              COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/openssl_builddata_test.py"
                "${CMAKE_SOURCE_DIR}"
                "${CPKT_DEPENDENCY_BUILD_ROOT}/openssl/src"
                "${CMAKE_BINARY_DIR}")
      cpkt_group_add_test(
              NAME clean_source_test_scratch
              COMMAND bash "${CMAKE_SOURCE_DIR}/tests/clean_source_test_scratch_test.sh"
                      "${CMAKE_SOURCE_DIR}")
      cpkt_group_set_tests_properties(clean_source_test_scratch PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      cpkt_group_add_test(NAME gssapi_facade COMMAND cpkt_gssapi_facade_test)
      cpkt_group_add_test(NAME gssapi_logging COMMAND cpkt_gssapi_logging_test)
      cpkt_group_set_tests_properties(gssapi_logging PROPERTIES ENVIRONMENT "KRB5CCNAME=FILE:${CMAKE_CURRENT_BINARY_DIR}/missing-gss-trace-cache;KRB5_CONFIG=/dev/null" LABELS "facade;memcheck" TIMEOUT 30)
      cpkt_group_add_test(NAME gssapi_extended COMMAND cpkt_gssapi_extended_test)
      cpkt_group_add_test(NAME gssapi_lucid_conversion
              COMMAND cpkt_gssapi_lucid_conversion_test)
      cpkt_group_add_test(NAME gssapi_iov_native_peer
              COMMAND cpkt_gssapi_iov_native_peer_test)
      cpkt_group_add_test(NAME gssapi_oid_native_parity
              COMMAND cpkt_gssapi_oid_native_parity_test)
      cpkt_group_set_tests_properties(gssapi_extended PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
      cpkt_group_set_tests_properties(gssapi_lucid_conversion PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
      cpkt_group_set_tests_properties(gssapi_iov_native_peer PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
      cpkt_group_set_tests_properties(gssapi_oid_native_parity PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
      if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(_cpkt_auth_symbol_format macho)
      else()
        set(_cpkt_auth_symbol_format elf)
      endif()
      cpkt_group_add_test(NAME gssapi_export_policy
              COMMAND bash "${CMAKE_SOURCE_DIR}/tests/auth_export_policy_test.sh"
                gssapi "$<TARGET_FILE:cpkt_gssapi_shared>"
                "${CMAKE_SOURCE_DIR}/cmake/exports/cpkt_gssapi.txt"
                "${_cpkt_auth_symbol_format}" "${CMAKE_NM}"
                "${CMAKE_C_COMPILER}" "${CMAKE_SYSROOT}" "${CMAKE_SOURCE_DIR}"
                "${CPKT_EXTERNAL_ROOT}")
      cpkt_group_add_test(NAME sasl_export_policy
              COMMAND bash "${CMAKE_SOURCE_DIR}/tests/auth_export_policy_test.sh"
                sasl "$<TARGET_FILE:cpkt_sasl_shared>"
                "${CMAKE_SOURCE_DIR}/cmake/exports/cpkt_sasl.txt"
                "${_cpkt_auth_symbol_format}" "${CMAKE_NM}"
                "${CMAKE_C_COMPILER}" "${CMAKE_SYSROOT}" "${CMAKE_SOURCE_DIR}"
                "${CPKT_EXTERNAL_ROOT}")
      cpkt_group_set_tests_properties(gssapi_export_policy sasl_export_policy PROPERTIES LABELS "facade;packaging" TIMEOUT 30)
      if(CPKT_TARGET_ID STREQUAL "x86_64-linux-gnu")
        cpkt_group_add_test(NAME contracts_out_of_source
                  COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/contracts_out_of_source_test.py"
                    "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" "${CMAKE_C_COMPILER}")
        cpkt_group_set_tests_properties(contracts_out_of_source PROPERTIES LABELS "facade;packaging" TIMEOUT 240)
      endif()
      if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(_cpkt_db_symbol_format macho)
      else()
        set(_cpkt_db_symbol_format elf)
      endif()
      cpkt_group_add_test(NAME auth_native_api_contract
              COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/scripts/auth_api_contract.py"
                --target "${CPKT_TARGET_ID}")
      cpkt_group_add_test(NAME auth_facade_binding_contract
              COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/scripts/auth_facade_bindings.py"
                --target "${CPKT_TARGET_ID}")
      cpkt_group_add_test(NAME auth_facade_signature_contract
              COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/scripts/auth_facade_signatures.py"
                "${CPKT_TARGET_ID}")
      cpkt_group_add_test(NAME auth_sasl_property_contract
              COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/scripts/auth_sasl_property_contract.py"
                "${CPKT_TARGET_ID}")
      if(CPKT_TARGET_ID STREQUAL "x86_64-linux-gnu")
        cpkt_group_add_test(NAME auth_contract_mutations
                  COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/auth_contract_mutation_test.py")
        cpkt_group_set_tests_properties(auth_contract_mutations PROPERTIES LABELS "facade;packaging" TIMEOUT 120 ENVIRONMENT "CPKT_CONFIGURED_BINARY_DIR=${CMAKE_BINARY_DIR}")
        foreach(_cpkt_catalog_generator IN ITEMS Ninja Makefiles)
          if(_cpkt_catalog_generator STREQUAL "Ninja")
            set(_cpkt_catalog_generator_name "Ninja")
          else()
            set(_cpkt_catalog_generator_name "Unix Makefiles")
          endif()
          cpkt_group_add_test(NAME export_catalog_dependency_${_cpkt_catalog_generator}
                      COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_SOURCE_DIR}/tests/export_catalog_dependency_test.py"
                        --configured-binary-dir "${CMAKE_BINARY_DIR}"
                        --generator "${_cpkt_catalog_generator_name}")
          cpkt_group_set_tests_properties(export_catalog_dependency_${_cpkt_catalog_generator} PROPERTIES LABELS "facade;packaging" TIMEOUT 180)
        endforeach()
      endif()
      cpkt_group_set_tests_properties(auth_native_api_contract auth_facade_binding_contract auth_facade_signature_contract auth_sasl_property_contract PROPERTIES LABELS "facade;packaging" TIMEOUT 120 ENVIRONMENT "CPKT_CONFIGURED_BINARY_DIR=${CMAKE_BINARY_DIR}")
      cpkt_group_add_test(NAME krb5_error_format COMMAND cpkt_krb5_error_format_test)
      cpkt_group_set_tests_properties(krb5_error_format PROPERTIES ENVIRONMENT "KRB5_CONFIG=/dev/null;LC_ALL=C")
      cpkt_group_add_test(NAME sasl_facade COMMAND cpkt_sasl_facade_test)
      cpkt_group_add_test(NAME sasl_plugin_facade COMMAND cpkt_sasl_plugin_facade_test)
      cpkt_group_add_test(NAME sasl_property_lifetime
              COMMAND cpkt_sasl_property_lifetime_test)
      cpkt_group_add_test(NAME sasl_property_coherence
              COMMAND cpkt_sasl_property_coherence_test)
      cpkt_group_add_test(NAME sasl_connection_property
              COMMAND cpkt_sasl_connection_property_test)
      cpkt_group_add_test(NAME sasl_idle_registration
              COMMAND cpkt_sasl_idle_registration_test)
      cpkt_group_set_tests_properties(sasl_idle_registration PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
      cpkt_group_add_test(NAME sasl_registration_failure
              COMMAND cpkt_sasl_registration_failure_test)
      cpkt_group_set_tests_properties(sasl_registration_failure PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
      if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        cpkt_group_add_test(NAME sasl_utility_allocation_failure
                  COMMAND cpkt_sasl_utility_allocation_failure_test)
        cpkt_group_set_tests_properties(sasl_utility_allocation_failure PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
      endif()
      cpkt_group_set_tests_properties(sasl_property_lifetime sasl_property_coherence PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
      cpkt_group_set_tests_properties(sasl_connection_property PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
      cpkt_group_set_tests_properties(sasl_plugin_facade PROPERTIES LABELS "facade;memcheck" TIMEOUT 30)
      file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/sasl-empty-plugin-path")
      cpkt_group_add_test(NAME sasl_privileged_plugin_path
              COMMAND cpkt_sasl_privileged_plugin_path_test privileged)
      cpkt_group_add_test(NAME sasl_ordinary_plugin_path
              COMMAND cpkt_sasl_privileged_plugin_path_test ordinary)
      cpkt_group_set_tests_properties(sasl_privileged_plugin_path sasl_ordinary_plugin_path PROPERTIES ENVIRONMENT "SASL_PATH=${CMAKE_BINARY_DIR}/sasl-empty-plugin-path")
      cpkt_group_add_test(NAME sasl_callback_retry COMMAND cpkt_sasl_callback_retry_test)
      set(_cpkt_memcheck_facade_tests lua_facade_unit lua_runtime_unit openssl_facade nghttp2_facade libssh2_header_facade libssh2_facade mqttc_header_facade mqttc_facade gssapi_facade gssapi_extended krb5_error_format sasl_facade sasl_callback_retry sasl_plugin_facade)
      if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND _cpkt_memcheck_facade_tests
                  libssh2_sftp_eof libssh2_poll_elapsed)
      endif()
      foreach(_cpkt_memcheck_test IN LISTS _cpkt_memcheck_facade_tests)
        cpkt_group_set_property(TEST ${_cpkt_memcheck_test} APPEND PROPERTY LABELS
                  facade memcheck)
      endforeach()
      cpkt_group_add_test(NAME lua_runtime_mock_unit COMMAND cpkt_lua_runtime_mock_test)
      cpkt_group_set_tests_properties(lua_runtime_mock_unit PROPERTIES LABELS "facade;memcheck")
      cpkt_group_add_test(
              NAME static_dependency_pic_policy
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/static_dependency_pic_policy_test.sh")
      cpkt_group_add_test(
              NAME linux_toolchain_sysroot_policy
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/linux_toolchain_sysroot_policy_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME osxcross_linker_cache_refresh
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/osxcross_linker_cache_refresh_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME repo_dependency_cache_policy
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/repo_dependency_cache_policy_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME repo_dependency_stale_root
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/repo_dependency_stale_root_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME curl_system_truststore
              COMMAND
                "${CMAKE_COMMAND}"
                "-DCPKT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
                "-DCPKT_TEST_BINARY_DIR=${CMAKE_BINARY_DIR}/curl-system-truststore-fixture"
                -P "${CMAKE_SOURCE_DIR}/tests/curl_system_truststore_test.cmake")
      cpkt_group_add_test(
              NAME external_project_toolchain_environment
              COMMAND
                "${CMAKE_COMMAND}"
                "-DCPKT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
                "-DCPKT_TEST_BINARY_DIR=${CMAKE_BINARY_DIR}/external-project-toolchain-env-fixture"
                -P "${CMAKE_SOURCE_DIR}/tests/external_project_toolchain_env_test.cmake")
      cpkt_group_add_test(
              NAME pinned_toolchain_resolvers
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/toolchain_resolver_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME lifecycle_toolchain_resolvers
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/toolchain_resolver_test.sh"
                "${CMAKE_SOURCE_DIR}"
                "${CMAKE_SOURCE_DIR}/skills/pkt-systems-cmake-lifecycle/scripts/cpkt-toolchains.sh")
      cpkt_group_add_test(
              NAME aflpp_resolver_cleanup
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/aflpp_resolver_cleanup_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME dependency_archive_cache
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/dependency_archive_cache_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(NAME dependency_cache_network_hits
              COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/dependency_cache_network_hits_test.py"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_set_tests_properties(dependency_cache_network_hits PROPERTIES LABELS "packaging;smoke" TIMEOUT 60)
      cpkt_group_add_test(
              NAME dependency_cache_disabled
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/dependency_cache_disabled_test.sh"
                "${CMAKE_SOURCE_DIR}"
                "${CMAKE_SOURCE_DIR}/build/dependency-cache-disabled-external"
                "${CMAKE_SOURCE_DIR}/build/dependency-cache-disabled-build")
      cpkt_group_add_test(
              NAME darwin_install_name_normalization
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/darwin_install_name_normalization_test.sh"
                "${CMAKE_SOURCE_DIR}"
                "${CMAKE_COMMAND}")
      cpkt_group_set_tests_properties(darwin_install_name_normalization PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      if(CPKT_TARGET_ID STREQUAL "x86_64-linux-gnu")
        cpkt_group_add_test(
                  NAME krb5_tls_module_smoke
                  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/krb5_tls_module_smoke.sh"
                    "${CMAKE_SOURCE_DIR}" "${CMAKE_C_COMPILER}"
                    "${CPKT_EXTERNAL_ROOT}/krb5/install/lib"
                    "${CPKT_EXTERNAL_ROOT}/openssl/install/lib")
        cpkt_group_set_tests_properties(krb5_tls_module_smoke PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
        cpkt_group_add_test(
                  NAME dependency_reuse_configure
                  COMMAND
                    "${CMAKE_SOURCE_DIR}/tests/dependency_reuse_configure_test.sh"
                    "${CMAKE_SOURCE_DIR}"
                    "${CMAKE_COMMAND}"
                    "${CMAKE_GENERATOR}"
                    "${CMAKE_MAKE_PROGRAM}"
                    "${CMAKE_TOOLCHAIN_FILE}"
                    "${CMAKE_BUILD_TYPE}")
        cpkt_group_set_tests_properties(dependency_reuse_configure PROPERTIES LABELS "packaging;smoke" TIMEOUT 60)
      endif()
      cpkt_group_add_test(
              NAME privacy_scan_failure
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/privacy_scan_failure_test.sh")
      cpkt_group_set_tests_properties(privacy_scan_failure PROPERTIES LABELS "packaging" TIMEOUT 30)
      cpkt_group_add_test(NAME source_archive_verify_failure
              COMMAND bash "${CMAKE_SOURCE_DIR}/tests/source_archive_verify_failure_test.sh")
      cpkt_group_set_tests_properties(source_archive_verify_failure PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      foreach(_cpkt_install_generator IN ITEMS Ninja Makefiles)
        if(_cpkt_install_generator STREQUAL "Makefiles")
          set(_cpkt_install_generator_name "Unix Makefiles")
        else()
          set(_cpkt_install_generator_name "Ninja")
        endif()
        cpkt_group_add_test(NAME dependency_install_order_${_cpkt_install_generator}
                  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/dependency_install_order_test.py"
                    "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" "${_cpkt_install_generator_name}")
        cpkt_group_set_tests_properties(dependency_install_order_${_cpkt_install_generator} PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      endforeach()
      cpkt_group_add_test(NAME zlib_feature_namespace
              COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/zlib_feature_namespace_test.py"
                "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_C_COMPILER}")
      cpkt_group_set_tests_properties(zlib_feature_namespace PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      cpkt_group_add_test(
              NAME dependency_component_targets
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/dependency_component_targets_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME krb5_feature_contract
              COMMAND bash "${CMAKE_SOURCE_DIR}/tests/krb5_feature_contract_test.sh")
      cpkt_group_set_tests_properties(krb5_feature_contract PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      cpkt_group_add_test(
              NAME package_assertions_elf_runpath
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/package_assertions_elf_runpath_test.sh")
      cpkt_group_set_tests_properties(package_assertions_elf_runpath PROPERTIES LABELS "packaging" TIMEOUT 30)
      cpkt_group_add_test(
              NAME package_assertions_cleanup
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/package_assertions_cleanup_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_set_tests_properties(package_assertions_cleanup PROPERTIES LABELS "packaging" TIMEOUT 30)
      cpkt_group_add_test(
              NAME package_failure_diagnostics
              COMMAND
                python3 "${CMAKE_SOURCE_DIR}/tests/package_failure_diagnostics_test.py"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_set_tests_properties(package_failure_diagnostics PROPERTIES LABELS "packaging" TIMEOUT 30)
      cpkt_group_add_test(
              NAME package_consumer_execution
              COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/package_consumer_execution_test.py"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_set_tests_properties(package_consumer_execution PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      cpkt_group_add_test(
              NAME package_archive_layout
              COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/package_archive_layout_test.py"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_set_tests_properties(package_archive_layout PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      cpkt_group_add_test(
              NAME package_verify_toolchain_tools
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/package_verify_toolchain_tools_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_set_tests_properties(package_verify_toolchain_tools PROPERTIES LABELS "packaging" TIMEOUT 30)
      cpkt_group_add_test(
              NAME native_hardening_host
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/native_hardening_host_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_set_tests_properties(native_hardening_host PROPERTIES LABELS "smoke" TIMEOUT 30)
      cpkt_group_add_test(
              NAME afl_crash_gate
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/run_afl_fuzz_crash_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_set_tests_properties(afl_crash_gate PROPERTIES LABELS "fuzz;smoke" TIMEOUT 30)
      cpkt_group_add_test(
              NAME fuzz_toolchain_policy
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/fuzz_toolchain_policy_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_set_tests_properties(fuzz_toolchain_policy PROPERTIES LABELS "fuzz;smoke" TIMEOUT 30)
      cpkt_group_add_test(NAME package_integration_contracts
              COMMAND "${CPKT_OPERATION_PYTHON}" "${CMAKE_SOURCE_DIR}/tests/package_integration_contract_test.py")
      cpkt_group_add_test(NAME github_actions_contracts
              COMMAND "${CPKT_OPERATION_PYTHON}" "${CMAKE_SOURCE_DIR}/tests/github_actions_contract_test.py")
      cpkt_group_add_test(
              NAME lifecycle_surface
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/lifecycle_surface_test.sh")
      cpkt_group_set_tests_properties(lifecycle_surface PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      cpkt_group_add_test(
              NAME package_install_smoke_args
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/package_install_smoke_args_test.sh")
      cpkt_group_set_tests_properties(package_install_smoke_args PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      cpkt_group_add_test(
              NAME mqttc_linker_flags
              COMMAND
                bash
                "${CMAKE_SOURCE_DIR}/tests/mqttc_linker_flags_test.sh")
      cpkt_group_set_tests_properties(mqttc_linker_flags PROPERTIES LABELS "packaging;smoke" TIMEOUT 30)
      cpkt_group_add_test(
              NAME facade_c89_target_policy
              COMMAND
                bash
                "${CMAKE_SOURCE_DIR}/tests/facade_c89_target_policy_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME lua_symbol_test_registration
              COMMAND
                bash
                "${CMAKE_SOURCE_DIR}/tests/lua_symbol_test_registration_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_set_tests_properties(lua_symbol_test_registration PROPERTIES ENVIRONMENT "CPKT_DEPENDENCY_CACHE=${CPKT_DEPENDENCY_CACHE}" TIMEOUT 60)
      if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(_cpkt_openssl_catalog_symbol_format macho)
        set(_cpkt_openssl_catalog_symbol_tool "${CMAKE_NM}")
      else()
        set(_cpkt_openssl_catalog_symbol_format elf)
        set(_cpkt_openssl_catalog_symbol_tool "${CMAKE_READELF}")
      endif()
      cpkt_group_add_test(
              NAME openssl_api_catalog
              COMMAND
                bash
                "${CMAKE_SOURCE_DIR}/tests/openssl_api_catalog_test.sh"
                "${CMAKE_SOURCE_DIR}"
                "${CPKT_DEPENDENCY_BUILD_ROOT}/openssl/src/util/libcrypto.num"
                "${CPKT_DEPENDENCY_BUILD_ROOT}/openssl/src/util/libssl.num"
                "$<TARGET_PROPERTY:cpkt::openssl_crypto_shared,IMPORTED_LOCATION>"
                "$<TARGET_PROPERTY:cpkt::openssl_ssl_shared,IMPORTED_LOCATION>"
                "${_cpkt_openssl_catalog_symbol_format}"
                "${_cpkt_openssl_catalog_symbol_tool}")
      if(CPKT_TARGET_ID STREQUAL "x86_64-linux-gnu")
        if(CPKT_OPENSSL_API_INVENTORY_CLANG AND
                    NOT EXISTS "${CPKT_OPENSSL_API_INVENTORY_CLANG}")
          unset(CPKT_OPENSSL_API_INVENTORY_CLANG CACHE)
        endif()
        find_program(CPKT_OPENSSL_API_INVENTORY_CLANG NAMES clang REQUIRED
                  NO_CMAKE_FIND_ROOT_PATH)
        cpkt_group_add_test(
                  NAME openssl_api_inventory
                  COMMAND
                    python3
                    "${CMAKE_SOURCE_DIR}/tools/generate_openssl_api_inventory.py"
                    --include-dir "$<TARGET_PROPERTY:cpkt::openssl_crypto_shared,INTERFACE_INCLUDE_DIRECTORIES>"
                    --num "${CPKT_DEPENDENCY_BUILD_ROOT}/openssl/src/util/libcrypto.num"
                    --num "${CPKT_DEPENDENCY_BUILD_ROOT}/openssl/src/util/libssl.num"
                    --library "$<TARGET_PROPERTY:cpkt::openssl_crypto_shared,IMPORTED_LOCATION>"
                    --library "$<TARGET_PROPERTY:cpkt::openssl_ssl_shared,IMPORTED_LOCATION>"
                    --symbol-format elf
                    --symbol-tool "${CMAKE_READELF}"
                    --clang "${CPKT_OPENSSL_API_INVENTORY_CLANG}"
                    --output "${CMAKE_BINARY_DIR}/openssl-api-inventory-test/inventory.json"
                    --work-dir "${CMAKE_BINARY_DIR}/openssl-api-inventory-test"
                    --facade-header "${CMAKE_SOURCE_DIR}/include/cpkt/openssl.h")
        cpkt_group_add_test(
                  NAME nghttp2_api_inventory
                  COMMAND
                    python3
                    "${CMAKE_SOURCE_DIR}/tools/generate_nghttp2_api_inventory.py"
                    --include-dir "$<TARGET_PROPERTY:cpkt::nghttp2_shared,INTERFACE_INCLUDE_DIRECTORIES>"
                    --library "$<TARGET_PROPERTY:cpkt::nghttp2_shared,IMPORTED_LOCATION>"
                    --symbol-format elf
                    --symbol-tool "${CMAKE_READELF}"
                    --clang "${CPKT_OPENSSL_API_INVENTORY_CLANG}"
                    --output "${CMAKE_BINARY_DIR}/nghttp2-api-inventory-test/inventory.json"
                    --work-dir "${CMAKE_BINARY_DIR}/nghttp2-api-inventory-test"
                    --facade-header "${CPKT_NGHTTP2_FACADE_HEADER}")
        cpkt_group_add_test(
                  NAME libssh2_api_inventory
                  COMMAND
                    python3
                    "${CMAKE_SOURCE_DIR}/tools/generate_libssh2_api_inventory.py"
                    --include-dir "$<TARGET_PROPERTY:cpkt::libssh2_shared,INTERFACE_INCLUDE_DIRECTORIES>"
                    --library "$<TARGET_PROPERTY:cpkt::libssh2_shared,IMPORTED_LOCATION>"
                    --symbol-tool "${CMAKE_NM}"
                    --clang "${CPKT_OPENSSL_API_INVENTORY_CLANG}"
                    --output "${CMAKE_BINARY_DIR}/libssh2-api-inventory-test/inventory.json"
                    --work-dir "${CMAKE_BINARY_DIR}/libssh2-api-inventory-test"
                    --facade-header "${CPKT_LIBSSH2_FACADE_HEADER}")
      endif()
      cpkt_group_add_test(
              NAME openssl_header_facade
              COMMAND
                bash
                "${CMAKE_SOURCE_DIR}/tests/openssl_header_facade_test.sh"
                "${CMAKE_SOURCE_DIR}"
                "${CMAKE_C_COMPILER}"
                "${CMAKE_SYSROOT}"
                "$<TARGET_PROPERTY:cpkt::openssl_crypto_shared,INTERFACE_INCLUDE_DIRECTORIES>")
      cpkt_group_add_test(
              NAME openssl_export_policy
              COMMAND
                bash
                "${CMAKE_SOURCE_DIR}/tests/openssl_export_policy_test.sh"
                "$<TARGET_FILE:cpkt_openssl_shared>"
                "${CMAKE_SYSTEM_NAME}"
                "${CMAKE_NM}"
                "${CMAKE_SOURCE_DIR}/cmake/exports/cpkt_openssl.txt"
                "${CMAKE_C_COMPILER}"
                "${CMAKE_SYSROOT}")
      cpkt_group_add_test(
              NAME nghttp2_header_facade
              COMMAND
                bash
                "${CMAKE_SOURCE_DIR}/tests/nghttp2_header_facade_test.sh"
                "${CMAKE_SOURCE_DIR}"
                "${CMAKE_C_COMPILER}"
                "${CMAKE_CXX_COMPILER}"
                "${CMAKE_SYSROOT}"
                "${CPKT_NGHTTP2_FACADE_INCLUDE_DIR}")
      cpkt_group_add_test(
              NAME nghttp2_export_policy
              COMMAND
                bash
                "${CMAKE_SOURCE_DIR}/tests/nghttp2_export_policy_test.sh"
                "$<TARGET_FILE:cpkt_nghttp2_shared>"
                "${CMAKE_SYSTEM_NAME}"
                "${CMAKE_NM}"
                "${CMAKE_SOURCE_DIR}/cmake/exports/cpkt_nghttp2.txt"
                "${CMAKE_C_COMPILER}"
                "${CMAKE_SYSROOT}")
      cpkt_group_add_test(
              NAME libssh2_export_policy
              COMMAND
                bash
                "${CMAKE_SOURCE_DIR}/tests/libssh2_export_policy_test.sh"
                "$<TARGET_FILE:cpkt_libssh2_shared>"
                "${CMAKE_SYSTEM_NAME}"
                "${CMAKE_NM}"
                "${CMAKE_SOURCE_DIR}/cmake/exports/cpkt_libssh2.txt"
                "${CMAKE_C_COMPILER}"
                "${CMAKE_SYSROOT}")
      cpkt_group_add_test(
              NAME mqttc_api_inventory
              COMMAND
                "${CPKT_PYTHON3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/generate_mqttc_api_inventory.py"
                --include-dir "$<TARGET_PROPERTY:cpkt::mqttc_static,INTERFACE_INCLUDE_DIRECTORIES>"
                --library "$<TARGET_FILE:cpkt::mqttc_shared>"
                --symbol-tool "${CMAKE_NM}"
                --facade-header "${CPKT_MQTTC_FACADE_HEADER}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/mqttc-api-inventory.json")
      cpkt_group_add_test(
              NAME mqttc_export_policy
              COMMAND
                bash
                "${CMAKE_SOURCE_DIR}/tests/mqttc_export_policy_test.sh"
                "$<TARGET_FILE:cpkt_mqttc_shared>"
                "${CMAKE_SYSTEM_NAME}"
                "${CMAKE_NM}"
                "${CMAKE_SOURCE_DIR}/cmake/exports/cpkt_mqttc.txt"
                "${CMAKE_C_COMPILER}"
                "${CMAKE_SYSROOT}")
      cpkt_group_add_test(
              NAME gssapi_header_facade
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/gssapi_header_facade_test.sh"
                "${CMAKE_SOURCE_DIR}")
      cpkt_group_add_test(
              NAME sasl_header_facade
              COMMAND
                "${CMAKE_SOURCE_DIR}/tests/sasl_header_facade_test.sh"
                "${CMAKE_SOURCE_DIR}")
      if(CPKT_TARGET_ID STREQUAL "x86_64-linux-gnu")
        cpkt_group_add_test(
                  NAME clangd_comment_gate
                  COMMAND
                    bash
                    "${CMAKE_SOURCE_DIR}/tests/clangd_comment_gate_test.sh"
                    "${CMAKE_SOURCE_DIR}/scripts/verify-clangd-surface.sh")
        cpkt_group_set_tests_properties(clangd_comment_gate PROPERTIES LABELS "smoke" TIMEOUT 30)
        cpkt_group_add_test(
                  NAME clangd_surface
                  COMMAND
                    "${CMAKE_SOURCE_DIR}/scripts/verify-clangd-surface.sh"
                    "${CMAKE_SOURCE_DIR}"
                    "${CMAKE_BINARY_DIR}")
      endif()
    endif()
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_cpkt_lua_inventory_symbol_format macho)
  else()
    set(_cpkt_lua_inventory_symbol_format elf)
  endif()
  cpkt_group_add_test(
      NAME lua_api_inventory
      COMMAND
        "${CPKT_PYTHON3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/generate_lua_api_inventory.py"
        --include-dir "$<TARGET_PROPERTY:cpkt::lua_static,INTERFACE_INCLUDE_DIRECTORIES>"
        --native-library "$<TARGET_FILE:cpkt::lua_shared>"
        --facade-library "$<TARGET_FILE:cpkt_lua_shared>"
        --facade-header "${CPKT_LUA_FACADE_HEADER}"
        --symbol-tool "${CMAKE_NM}"
        --symbol-format "${_cpkt_lua_inventory_symbol_format}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/lua-api-inventory.json")
  cpkt_group_add_test(
      NAME lua_gc_facade_generation
      COMMAND
        bash "${CMAKE_SOURCE_DIR}/tests/lua_gc_facade_generation_test.sh"
        "${CPKT_LUA_FACADE_SOURCE}")
  cpkt_group_add_test(
      NAME lua_export_policy
      COMMAND
        bash
        "${CMAKE_SOURCE_DIR}/tests/lua_export_policy_test.sh"
        "$<TARGET_FILE:cpkt_lua_shared>"
        "${CMAKE_SYSTEM_NAME}"
        "${CMAKE_NM}"
        "${CMAKE_SOURCE_DIR}/cmake/exports/cpkt_lua.txt"
        "${CMAKE_C_COMPILER}"
        "${CMAKE_SYSROOT}")
endif()
if(CPKT_ENABLE_FUZZING)
  cpkt_add_lua_runtime_mock_test(cpkt_lua_runtime_fuzz fuzz/lua_runtime_fuzz.c)
  cpkt_configure_afl_target(cpkt_lua_runtime_fuzz)
endif()
include(cmake/CpktPackage.cmake)
if(CPKT_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  foreach(linkage IN ITEMS static shared)
    cpkt_group_add_executable(cpkt_abi_smoke_${linkage} tests/release_smoke.c)
    cpkt_configure_c89_target(cpkt_abi_smoke_${linkage})
    cpkt_add_repo_warning_errors(cpkt_abi_smoke_${linkage})
    target_link_libraries(cpkt_abi_smoke_${linkage} PRIVATE cpkt_openssl_${linkage})
    set_target_properties(cpkt_abi_smoke_${linkage} PROPERTIES
          BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "@executable_path/../lib")
    cpkt_group_add_test(NAME release_smoke_${linkage}
          COMMAND "${CMAKE_COMMAND}" -E env
            "DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}:${CPKT_EXTERNAL_ROOT}/openssl/install/lib"
            "$<TARGET_FILE:cpkt_abi_smoke_${linkage}>")
  endforeach()
endif()
if(CPKT_BUILD_TESTS)
  cpkt_group_add_test(NAME repository_boundary
      COMMAND "${CPKT_OPERATION_PYTHON}" "${CMAKE_SOURCE_DIR}/tests/repository_boundary_test.py")
endif()

# Each linked shared module proves an owned static native/facade archive is PIC.
if(CPKT_BUILD_TESTS AND NOT CPKT_FACADE_ONLY)
  cpkt_group_add_library(cpkt_static_archive_pic_zlib SHARED tests/sdk-consumers/cpkt_zlib.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_zlib PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_zlib)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_zlib PRIVATE cpkt::zlib_static)
  cpkt_group_add_library(cpkt_static_archive_pic_nghttp2 SHARED tests/sdk-consumers/cpkt_nghttp2.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_nghttp2 PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_nghttp2)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_nghttp2 PRIVATE cpkt::nghttp2_static)
  cpkt_group_add_library(cpkt_static_archive_pic_crypto SHARED tests/sdk-consumers/cpkt_crypto.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_crypto PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_crypto)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_crypto PRIVATE cpkt::openssl_crypto_static)
  cpkt_group_add_library(cpkt_static_archive_pic_ssl SHARED tests/sdk-consumers/cpkt_ssl.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_ssl PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_ssl)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_ssl PRIVATE cpkt::openssl_ssl_static)
  cpkt_group_add_library(cpkt_static_archive_pic_openssl_facade SHARED tests/sdk-consumers/cpkt_openssl_facade_strict.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_openssl_facade PRIVATE -std=c89)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_openssl_facade)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_openssl_facade PRIVATE cpkt_openssl_static)
  cpkt_group_add_library(cpkt_static_archive_pic_nghttp2_facade SHARED tests/sdk-consumers/cpkt_nghttp2_facade_strict.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_nghttp2_facade PRIVATE -std=c89)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_nghttp2_facade)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_nghttp2_facade PRIVATE cpkt_nghttp2_static)
  cpkt_group_add_library(cpkt_static_archive_pic_libssh2_facade SHARED tests/sdk-consumers/cpkt_libssh2_facade_strict.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_libssh2_facade PRIVATE -std=c89)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_libssh2_facade)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_libssh2_facade PRIVATE cpkt_libssh2_static)
  cpkt_group_add_library(cpkt_static_archive_pic_mqttc_facade SHARED tests/sdk-consumers/cpkt_mqttc_facade_strict.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_mqttc_facade PRIVATE -std=c89)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_mqttc_facade)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_mqttc_facade PRIVATE cpkt_mqttc_static)
  cpkt_group_add_library(cpkt_static_archive_pic_libssh2 SHARED tests/sdk-consumers/cpkt_libssh2.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_libssh2 PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_libssh2)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_libssh2 PRIVATE cpkt::libssh2_static)
  cpkt_group_add_library(cpkt_static_archive_pic_curl SHARED tests/sdk-consumers/cpkt_curl.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_curl PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_curl)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_curl PRIVATE cpkt::curl_static)
  cpkt_group_add_library(cpkt_static_archive_pic_libxml2 SHARED tests/sdk-consumers/cpkt_libxml2.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_libxml2 PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_libxml2)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_libxml2 PRIVATE cpkt::libxml2_static)
  cpkt_group_add_library(cpkt_static_archive_pic_lua SHARED tests/sdk-consumers/cpkt_lua.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_lua PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_lua)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_lua PRIVATE cpkt::lua_static)
  cpkt_group_add_library(cpkt_static_archive_pic_lua_facade SHARED tests/sdk-consumers/cpkt_lua_facade_strict.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_lua_facade PRIVATE -std=c89)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_lua_facade)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_lua_facade PRIVATE cpkt_lua_static)
  cpkt_group_add_library(cpkt_static_archive_pic_mqttc SHARED tests/sdk-consumers/cpkt_mqttc.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_mqttc PRIVATE -std=c99)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_mqttc)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_mqttc PRIVATE cpkt::mqttc_static)
  cpkt_group_add_library(cpkt_static_archive_pic_gssapi_facade SHARED tests/sdk-consumers/cpkt_gssapi_facade_strict.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_gssapi_facade PRIVATE -std=c89)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_gssapi_facade)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_gssapi_facade PRIVATE cpkt_gssapi_static)
  cpkt_group_add_library(cpkt_static_archive_pic_sasl_facade SHARED tests/sdk-consumers/cpkt_sasl_facade_strict.c)
  cpkt_group_target_command(target_compile_options cpkt_static_archive_pic_sasl_facade PRIVATE -std=c89)
  cpkt_add_repo_warning_errors(cpkt_static_archive_pic_sasl_facade)
  cpkt_group_target_command(target_link_libraries cpkt_static_archive_pic_sasl_facade PRIVATE cpkt_sasl_static)
  if(CPKT_CAN_RUN_TARGET_EXECUTABLES)
    cpkt_group_add_test(NAME static_archive_pic_link
      COMMAND "${CMAKE_SOURCE_DIR}/tests/static_archive_pic_link_test.sh"
        "$<TARGET_FILE:cpkt_static_archive_pic_zlib>"
        "$<TARGET_FILE:cpkt_static_archive_pic_nghttp2>"
        "$<TARGET_FILE:cpkt_static_archive_pic_crypto>"
        "$<TARGET_FILE:cpkt_static_archive_pic_ssl>"
        "$<TARGET_FILE:cpkt_static_archive_pic_openssl_facade>"
        "$<TARGET_FILE:cpkt_static_archive_pic_nghttp2_facade>"
        "$<TARGET_FILE:cpkt_static_archive_pic_libssh2_facade>"
        "$<TARGET_FILE:cpkt_static_archive_pic_mqttc_facade>"
        "$<TARGET_FILE:cpkt_static_archive_pic_libssh2>"
        "$<TARGET_FILE:cpkt_static_archive_pic_curl>"
        "$<TARGET_FILE:cpkt_static_archive_pic_libxml2>"
        "$<TARGET_FILE:cpkt_static_archive_pic_lua>"
        "$<TARGET_FILE:cpkt_static_archive_pic_lua_facade>"
        "$<TARGET_FILE:cpkt_static_archive_pic_mqttc>"
        "$<TARGET_FILE:cpkt_static_archive_pic_gssapi_facade>"
        "$<TARGET_FILE:cpkt_static_archive_pic_sasl_facade>"
    )
  endif()
endif()
