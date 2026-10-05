cmake_minimum_required(VERSION 3.21)
project(cpkt_external_project_toolchain_env NONE)

if(NOT DEFINED CPKT_SOURCE_DIR OR "${CPKT_SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CPKT_SOURCE_DIR is required")
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CPKT_BUILD_DEPENDENCIES ON)
set(test_toolchain_root "${CMAKE_BINARY_DIR}/bootlin")
file(MAKE_DIRECTORY "${test_toolchain_root}/bin")
set(CPKT_TOOLCHAIN_ROOT "${test_toolchain_root}")
set(CPKT_EXTERNAL_ROOT "${CMAKE_BINARY_DIR}/deps")
set(CMAKE_C_COMPILER "${test_toolchain_root}/bin/x86_64-linux-gcc")
set(CMAKE_CXX_COMPILER "${test_toolchain_root}/bin/x86_64-linux-g++")
set(CMAKE_LINKER "${test_toolchain_root}/bin/x86_64-linux-ld")
set(CMAKE_AR "${test_toolchain_root}/bin/x86_64-linux-ar")
set(CMAKE_RANLIB "${test_toolchain_root}/bin/x86_64-linux-ranlib")
set(CMAKE_STRIP "${test_toolchain_root}/bin/x86_64-linux-strip")
set(CMAKE_NM "${test_toolchain_root}/bin/x86_64-linux-nm")
set(CMAKE_OBJCOPY "${test_toolchain_root}/bin/x86_64-linux-objcopy")
set(CMAKE_OBJDUMP "${test_toolchain_root}/bin/x86_64-linux-objdump")
set(CMAKE_ADDR2LINE "${test_toolchain_root}/bin/x86_64-linux-addr2line")
set(CMAKE_READELF "${test_toolchain_root}/bin/x86_64-linux-readelf")

include("${CPKT_SOURCE_DIR}/cmake/CpktDependencies.cmake")
set(environment_args "CFLAGS=-O2")
cpkt_append_pinned_external_toolchain_env_args(environment_args)

foreach(expected IN ITEMS
    "PATH=${test_toolchain_root}/bin:$ENV{PATH}"
    "CC=${CMAKE_C_COMPILER}"
    "CXX=${CMAKE_CXX_COMPILER}"
    "LD=${CMAKE_LINKER}"
    "AR=${CMAKE_AR}"
    "RANLIB=${CMAKE_RANLIB}"
    "STRIP=${CMAKE_STRIP}"
    "NM=${CMAKE_NM}"
    "OBJCOPY=${CMAKE_OBJCOPY}"
    "OBJDUMP=${CMAKE_OBJDUMP}"
    "ADDR2LINE=${CMAKE_ADDR2LINE}"
    "READELF=${CMAKE_READELF}")
  list(FIND environment_args "${expected}" found_index)
  if(found_index LESS 0)
    message(FATAL_ERROR "External-project environment did not pin ${expected}")
  endif()
endforeach()

set(cmake_args "")
cpkt_append_common_external_cmake_args(cmake_args)
foreach(expected IN ITEMS
    "-DCMAKE_LINKER=${CMAKE_LINKER}"
    "-DCMAKE_NM=${CMAKE_NM}"
    "-DCMAKE_STRIP=${CMAKE_STRIP}"
    "-DCMAKE_READELF=${CMAKE_READELF}")
  list(FIND cmake_args "${expected}" found_index)
  if(found_index LESS 0)
    message(FATAL_ERROR "External CMake arguments did not pin ${expected}")
  endif()
endforeach()

set(CMAKE_SYSTEM_NAME Darwin)
set(CPKT_OSXCROSS_HOST arm64-apple-darwin25.4)
set(CPKT_MACOS_DEPLOYMENT_TARGET 15.0)
cpkt_get_autotools_link_flags(darwin_link_flags)
string(FIND "${darwin_link_flags}" "-Wl,-not_for_dyld_shared_cache" shared_cache_flag_index)
if(shared_cache_flag_index LESS 0)
  message(FATAL_ERROR "Bundled Darwin Autotools dylibs must opt out of the system shared cache")
endif()
set(CMAKE_CROSSCOMPILING TRUE)
cpkt_get_autotools_link_flags(darwin_cross_link_flags)
string(FIND "${darwin_cross_link_flags}" "-Wl,-not_for_dyld_shared_cache" cross_shared_cache_flag_index)
if(NOT cross_shared_cache_flag_index LESS 0)
  message(FATAL_ERROR "osxcross linker received an unsupported shared-cache flag")
endif()
set(CMAKE_CROSSCOMPILING FALSE)
cpkt_append_common_external_cmake_args(darwin_cmake_args)
foreach(expected IN ITEMS
    "-DCPKT_OSXCROSS_HOST=${CPKT_OSXCROSS_HOST}"
    "-DCPKT_MACOS_DEPLOYMENT_TARGET=${CPKT_MACOS_DEPLOYMENT_TARGET}"
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CPKT_MACOS_DEPLOYMENT_TARGET}")
  list(FIND darwin_cmake_args "${expected}" found_index)
  if(found_index LESS 0)
    message(FATAL_ERROR "Darwin ExternalProject arguments did not propagate ${expected}")
  endif()
endforeach()
set(darwin_env_args "")
cpkt_append_darwin_external_env_args(darwin_env_args)
list(FIND darwin_env_args "MACOSX_DEPLOYMENT_TARGET=15.0" minimum_env_index)
if(minimum_env_index LESS 0)
  message(FATAL_ERROR "Darwin external builds omit the deployment target")
endif()
foreach(unexpected IN ITEMS
    "PATH=:$ENV{PATH}"
    "LD_LIBRARY_PATH=/lib:$ENV{LD_LIBRARY_PATH}"
    "LDFLAGS=--ld-path=${CMAKE_LINKER}")
  list(FIND darwin_env_args "${unexpected}" unexpected_index)
  if(unexpected_index GREATER_EQUAL 0)
    message(FATAL_ERROR "Native Darwin external build received ${unexpected}")
  endif()
endforeach()
cpkt_get_external_c_flags(darwin_c_flags)
if("core" STREQUAL "misc")
  cpkt_get_external_cxx_flags(darwin_cxx_flags)
endif()
set(darwin_compiler_flags "${darwin_c_flags}")
if("core" STREQUAL "misc")
  list(APPEND darwin_compiler_flags "${darwin_cxx_flags}")
endif()
foreach(flags IN LISTS darwin_compiler_flags)
  string(FIND "${flags}" "-mmacosx-version-min=15.0" minimum_flag_index)
  if(minimum_flag_index LESS 0)
    message(FATAL_ERROR "Darwin external compiler flags omit the deployment target")
  endif()
endforeach()
# Direct Apple compiler invocations need the selected SDK even when a
# system compiler dispatcher would have supplied it implicitly.
set(CMAKE_OSX_SYSROOT "${CMAKE_BINARY_DIR}/selected SDK/MacOSX.sdk")
file(MAKE_DIRECTORY "${CMAKE_OSX_SYSROOT}")
foreach(cross IN ITEMS FALSE TRUE)
  set(CMAKE_CROSSCOMPILING ${cross})
  set(sdk_env_args "")
  cpkt_append_darwin_external_env_args(sdk_env_args)
  list(FIND sdk_env_args "SDKROOT=${CMAKE_OSX_SYSROOT}" sdk_env_index)
  if(sdk_env_index LESS 0)
    message(FATAL_ERROR "Darwin external builds omit the selected SDK environment")
  endif()
  cpkt_append_common_external_cmake_args(sdk_cmake_args)
  list(FIND sdk_cmake_args "-DCMAKE_OSX_SYSROOT:PATH=${CMAKE_OSX_SYSROOT}" sdk_cmake_index)
  if(sdk_cmake_index LESS 0)
    message(FATAL_ERROR "Darwin external CMake builds omit the selected SDK")
  endif()
  cpkt_get_external_c_flags(sdk_c_flags)
  set(sdk_compiler_flags "${sdk_c_flags}")
  foreach(flags IN LISTS sdk_compiler_flags)
    separate_arguments(arguments UNIX_COMMAND "${flags}")
    list(FIND arguments "-isysroot" sdk_flag_index)
    if(sdk_flag_index LESS 0)
      message(FATAL_ERROR "Darwin external compiler flags omit the selected SDK")
    endif()
    math(EXPR sdk_path_index "${sdk_flag_index} + 1")
    list(GET arguments ${sdk_path_index} sdk_path)
    if(NOT sdk_path STREQUAL CMAKE_OSX_SYSROOT)
      message(FATAL_ERROR "Darwin external compiler split or changed the SDK path")
    endif()
  endforeach()
endforeach()
set(CMAKE_CROSSCOMPILING FALSE)
set(CMAKE_SYSTEM_NAME Linux)

set(CPKT_TARGET_ARCH x86_64)
set(CPKT_TARGET_LIBC gnu)
set(CPKT_TARGET_OS Linux)
if("core" STREQUAL "core")
cpkt_get_openssl_config_args(openssl_config_args)
set(expected_openssl_rpath "-Wl,--enable-new-dtags,-rpath,\\\\$$ORIGIN")
list(FIND openssl_config_args "${expected_openssl_rpath}" found_index)
if(found_index LESS 0)
  message(FATAL_ERROR
    "OpenSSL Configure arguments must preserve \\$$ORIGIN for its generated GNU Makefile")
endif()

endif()
