cmake_minimum_required(VERSION 3.21)
project(cpkt_curl_system_truststore NONE)

if(NOT DEFINED CPKT_SOURCE_DIR OR CPKT_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "CPKT_SOURCE_DIR is required")
endif()

include("${CPKT_SOURCE_DIR}/cmake/CpktDependencies.cmake")

foreach(target IN ITEMS
    x86_64-linux-gnu
    x86_64-linux-musl
    aarch64-linux-gnu
    aarch64-linux-musl
    armhf-linux-gnu
    armhf-linux-musl)
  string(REPLACE "-" ";" target_parts "${target}")
  list(GET target_parts 0 CPKT_TARGET_ARCH)
  list(GET target_parts 2 CPKT_TARGET_LIBC)
  set(CPKT_TARGET_OS linux)
  set(CMAKE_SYSTEM_NAME Linux)

  cpkt_get_target_triple(target_triple)
  cpkt_get_curl_platform_cmake_args(curl_cmake_args)
  list(FIND curl_cmake_args "-DCURL_CA_FALLBACK=ON" fallback_index)
  if(fallback_index LESS 0)
    message(FATAL_ERROR
      "${target} (${target_triple}) curl configuration does not enable OpenSSL system trust-store fallback")
  endif()
  foreach(disallowed_arg IN ITEMS
      "-DCURL_CA_BUNDLE=none"
      "-DCURL_CA_PATH=none")
    list(FIND curl_cmake_args "${disallowed_arg}" disallowed_index)
    if(disallowed_index GREATER_EQUAL 0)
      message(FATAL_ERROR
        "${target} (${target_triple}) curl configuration disables CA discovery with ${disallowed_arg}")
    endif()
  endforeach()
endforeach()

set(CPKT_TARGET_ARCH arm64)
set(CPKT_TARGET_OS darwin)
set(CPKT_TARGET_LIBC "")
set(CMAKE_SYSTEM_NAME Darwin)

cpkt_get_target_triple(target_triple)
cpkt_get_curl_platform_cmake_args(curl_cmake_args)
cpkt_get_curl_static_platform_libs(curl_static_platform_libs)
list(FIND curl_cmake_args "-DUSE_APPLE_SECTRUST=ON" sectrust_index)
if(sectrust_index LESS 0)
  message(FATAL_ERROR
    "arm64-apple-darwin (${target_triple}) curl configuration does not enable Apple SecTrust")
endif()
list(FIND curl_cmake_args "-DENABLE_THREADED_RESOLVER=OFF" resolver_off_index)
if(resolver_off_index GREATER_EQUAL 0)
  message(FATAL_ERROR
    "arm64-apple-darwin (${target_triple}) curl platform overrides asynchronous DNS")
endif()
foreach(disallowed_arg IN ITEMS
    "-DCURL_CA_BUNDLE=none"
    "-DCURL_CA_PATH=none")
  list(FIND curl_cmake_args "${disallowed_arg}" disallowed_index)
  if(disallowed_index GREATER_EQUAL 0)
    message(FATAL_ERROR
      "arm64-apple-darwin (${target_triple}) curl configuration disables CA discovery with ${disallowed_arg}")
  endif()
endforeach()
foreach(required_framework IN ITEMS
    "-framework SystemConfiguration"
    "-framework Security"
    "-framework CoreFoundation"
    "-framework CoreServices")
  list(FIND curl_static_platform_libs "${required_framework}" framework_index)
  if(framework_index LESS 0)
    message(FATAL_ERROR
      "arm64-apple-darwin (${target_triple}) curl static link metadata omits ${required_framework}")
  endif()
endforeach()

message(STATUS "curl system trust-store configuration passed")
