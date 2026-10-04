set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    CPKT_OSXCROSS_HOST
    CPKT_MACOS_DEPLOYMENT_TARGET)

if(DEFINED ENV{OSXCROSS_ROOT} AND NOT "$ENV{OSXCROSS_ROOT}" STREQUAL "")
    set(CPKT_OSXCROSS_ROOT "$ENV{OSXCROSS_ROOT}")
elseif(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
    set(CPKT_OSXCROSS_ROOT "$ENV{HOME}/.local/cross/osxcross")
else()
    message(FATAL_ERROR "OSXCROSS_ROOT is not set and HOME is unavailable")
endif()

if((NOT DEFINED CPKT_OSXCROSS_HOST OR CPKT_OSXCROSS_HOST STREQUAL "")
    AND DEFINED ENV{CPKT_OSXCROSS_HOST} AND NOT "$ENV{CPKT_OSXCROSS_HOST}" STREQUAL "")
    set(CPKT_OSXCROSS_HOST "$ENV{CPKT_OSXCROSS_HOST}")
endif()
get_filename_component(_cpkt_toolchain_dir "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(_cpkt_cmake_dir "${_cpkt_toolchain_dir}" DIRECTORY)
get_filename_component(_cpkt_repo_root "${_cpkt_cmake_dir}" DIRECTORY)
set(_cpkt_toolchain_resolver "${_cpkt_repo_root}/scripts/cpkt-toolchains.sh")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      "OSXCROSS_ROOT=${CPKT_OSXCROSS_ROOT}"
      "CPKT_OSXCROSS_HOST=${CPKT_OSXCROSS_HOST}"
      "${_cpkt_toolchain_resolver}" discover arm64-apple-darwin
    RESULT_VARIABLE _cpkt_darwin_toolchain_result
    OUTPUT_VARIABLE _cpkt_darwin_toolchain_report
    ERROR_VARIABLE _cpkt_darwin_toolchain_error)
string(ASCII 13 _cpkt_carriage_return)
string(REPLACE "${_cpkt_carriage_return}" "" _cpkt_darwin_toolchain_report "${_cpkt_darwin_toolchain_report}")
string(FIND "${_cpkt_darwin_toolchain_report}" "status=ready" _cpkt_darwin_toolchain_ready_index)
if(NOT _cpkt_darwin_toolchain_result EQUAL 0
    OR _cpkt_darwin_toolchain_ready_index LESS 0)
    message(FATAL_ERROR
        "arm64 Apple Darwin requires the local osxcross SDK and the pinned host MIG toolchain.\n"
        "Run: ${_cpkt_toolchain_resolver} ensure arm64-apple-darwin\n"
        "${_cpkt_darwin_toolchain_error}${_cpkt_darwin_toolchain_report}")
endif()
string(REPLACE "\n" ";" _cpkt_darwin_toolchain_lines "${_cpkt_darwin_toolchain_report}")
foreach(_cpkt_darwin_toolchain_line IN LISTS _cpkt_darwin_toolchain_lines)
    if(_cpkt_darwin_toolchain_line MATCHES "^prefix=(.+)$")
        set(_cpkt_darwin_host "${CMAKE_MATCH_1}")
    elseif(_cpkt_darwin_toolchain_line MATCHES "^mig=(.+)$")
        set(_cpkt_darwin_mig "${CMAKE_MATCH_1}")
    elseif(_cpkt_darwin_toolchain_line MATCHES "^migcom=(.+)$")
        set(_cpkt_darwin_migcom "${CMAKE_MATCH_1}")
    elseif(_cpkt_darwin_toolchain_line MATCHES "^mig_revision=(.+)$")
        set(_cpkt_darwin_mig_revision "${CMAKE_MATCH_1}")
    endif()
endforeach()
if(NOT DEFINED _cpkt_darwin_host OR NOT DEFINED _cpkt_darwin_mig OR NOT DEFINED _cpkt_darwin_migcom OR NOT DEFINED _cpkt_darwin_mig_revision)
    message(FATAL_ERROR "Pinned Darwin toolchain resolver did not report complete host MIG metadata")
endif()
set(CPKT_OSXCROSS_HOST "${_cpkt_darwin_host}")
set(ENV{CPKT_OSXCROSS_HOST} "${CPKT_OSXCROSS_HOST}")
set(CPKT_DARWIN_HOST_MIG "${_cpkt_darwin_mig}" CACHE FILEPATH "Pinned host MIG wrapper." FORCE)
set(CPKT_DARWIN_HOST_MIGCOM "${_cpkt_darwin_migcom}" CACHE FILEPATH "Pinned host MIG compiler." FORCE)
set(CPKT_DARWIN_HOST_MIG_REVISION "${_cpkt_darwin_mig_revision}" CACHE INTERNAL "Pinned host MIG source revision." FORCE)
set(CPKT_MACOS_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS deployment target")
set(CMAKE_OSX_DEPLOYMENT_TARGET "${CPKT_MACOS_DEPLOYMENT_TARGET}" CACHE STRING "" FORCE)

set(CPKT_OSXCROSS_BIN_DIR "${CPKT_OSXCROSS_ROOT}/bin")
set(ENV{PATH} "${CPKT_OSXCROSS_BIN_DIR}:$ENV{PATH}")
set(CMAKE_C_COMPILER "${CPKT_OSXCROSS_BIN_DIR}/${CPKT_OSXCROSS_HOST}-clang" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${CPKT_OSXCROSS_BIN_DIR}/${CPKT_OSXCROSS_HOST}-clang++" CACHE FILEPATH "" FORCE)
set(CMAKE_AR "${CPKT_OSXCROSS_BIN_DIR}/${CPKT_OSXCROSS_HOST}-ar" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${CPKT_OSXCROSS_BIN_DIR}/${CPKT_OSXCROSS_HOST}-ranlib" CACHE FILEPATH "" FORCE)
set(CMAKE_LINKER "${CPKT_OSXCROSS_BIN_DIR}/${CPKT_OSXCROSS_HOST}-ld" CACHE FILEPATH "" FORCE)
set(CMAKE_INSTALL_NAME_TOOL "${CPKT_OSXCROSS_BIN_DIR}/${CPKT_OSXCROSS_HOST}-install_name_tool" CACHE FILEPATH "" FORCE)
set(CPKT_OTOOL "${CPKT_OSXCROSS_BIN_DIR}/${CPKT_OSXCROSS_HOST}-otool" CACHE FILEPATH "" FORCE)

foreach(_cpkt_required_tool
        CMAKE_C_COMPILER
        CMAKE_AR
        CMAKE_RANLIB
        CMAKE_LINKER
        CMAKE_INSTALL_NAME_TOOL
        CPKT_OTOOL)
    if(NOT EXISTS "${${_cpkt_required_tool}}")
        message(FATAL_ERROR
            "The arm64 Apple Darwin osxcross toolchain is missing ${_cpkt_required_tool}: "
            "${${_cpkt_required_tool}}. Set OSXCROSS_ROOT or install osxcross under $HOME/.local/cross/osxcross.")
    endif()
endforeach()

set(_cpkt_darwin_linker_flag "--ld-path=${CMAKE_LINKER}")
foreach(_cpkt_linker_flags
        CMAKE_EXE_LINKER_FLAGS
        CMAKE_SHARED_LINKER_FLAGS
        CMAKE_MODULE_LINKER_FLAGS)
    set(_cpkt_existing_linker_flags "${${_cpkt_linker_flags}}")
    string(REGEX REPLACE "(^| )--ld-path=[^ ]+" " " _cpkt_existing_linker_flags "${_cpkt_existing_linker_flags}")
    string(REGEX REPLACE "(^| )-fuse-ld=[^ ]+" " " _cpkt_existing_linker_flags "${_cpkt_existing_linker_flags}")
    string(STRIP "${_cpkt_existing_linker_flags}" _cpkt_existing_linker_flags)
    if("${_cpkt_existing_linker_flags}" STREQUAL "")
        set(_cpkt_existing_linker_flags "${_cpkt_darwin_linker_flag}")
    else()
        set(_cpkt_existing_linker_flags "${_cpkt_darwin_linker_flag} ${_cpkt_existing_linker_flags}")
    endif()
    if(NOT "${_cpkt_existing_linker_flags}" STREQUAL "${${_cpkt_linker_flags}}")
        set(${_cpkt_linker_flags} "${_cpkt_existing_linker_flags}" CACHE STRING "" FORCE)
    endif()
endforeach()
unset(_cpkt_linker_flags)
unset(_cpkt_darwin_linker_flag)
unset(_cpkt_existing_linker_flags)

file(GLOB _cpkt_osxcross_sdks LIST_DIRECTORIES true "${CPKT_OSXCROSS_ROOT}/SDK/MacOSX*.sdk")
if(NOT _cpkt_osxcross_sdks)
    message(FATAL_ERROR "failed to locate a usable osxcross macOS SDK under ${CPKT_OSXCROSS_ROOT}/SDK")
endif()
list(SORT _cpkt_osxcross_sdks)
list(REVERSE _cpkt_osxcross_sdks)
list(GET _cpkt_osxcross_sdks 0 CPKT_OSXCROSS_SDK)
if(NOT EXISTS "${CPKT_OSXCROSS_SDK}/usr/include")
    message(FATAL_ERROR "failed to locate a usable osxcross macOS SDK under ${CPKT_OSXCROSS_ROOT}/SDK")
endif()

set(CMAKE_OSX_SYSROOT "${CPKT_OSXCROSS_SDK}" CACHE PATH "" FORCE)
set(CMAKE_FIND_ROOT_PATH "${CPKT_OSXCROSS_SDK}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CPKT_TARGET_ARCH arm64 CACHE STRING "" FORCE)
set(CPKT_TARGET_OS darwin CACHE STRING "" FORCE)
set(CPKT_TARGET_LIBC "" CACHE STRING "" FORCE)
unset(_cpkt_darwin_mig)
unset(_cpkt_darwin_host)
unset(_cpkt_darwin_migcom)
unset(_cpkt_darwin_mig_revision)
unset(_cpkt_darwin_toolchain_line)
unset(_cpkt_darwin_toolchain_lines)
unset(_cpkt_carriage_return)
unset(_cpkt_darwin_toolchain_error)
unset(_cpkt_darwin_toolchain_report)
unset(_cpkt_darwin_toolchain_result)
unset(_cpkt_darwin_toolchain_ready_index)
unset(_cpkt_toolchain_resolver)
unset(_cpkt_repo_root)
unset(_cpkt_cmake_dir)
unset(_cpkt_toolchain_dir)
