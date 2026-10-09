include(ExternalProject)

include("${CMAKE_CURRENT_LIST_DIR}/CpktDependencyArchiveCache.cmake")
function(cpkt_external_install_byproducts out_var)
if(CMAKE_VERSION VERSION_LESS 3.26)
set(${out_var} ${ARGN} PARENT_SCOPE)
else()
set(${out_var} INSTALL_BYPRODUCTS ${ARGN} PARENT_SCOPE)
endif()
endfunction()
macro(cpkt_cached_external_project_add)
set(_cpkt_ep_args ${ARGV})
list(FIND _cpkt_ep_args "URL" _cpkt_ep_url_index)
list(FIND _cpkt_ep_args "URL_HASH" _cpkt_ep_hash_index)
if(_cpkt_ep_url_index LESS 1 OR _cpkt_ep_hash_index LESS 0 OR _cpkt_ep_hash_index LESS _cpkt_ep_url_index)
message(FATAL_ERROR "cpkt_cached_external_project_add requires URL and URL_HASH after the target name")
endif()
math(EXPR _cpkt_ep_url_start "${_cpkt_ep_url_index} + 1")
math(EXPR _cpkt_ep_url_count "${_cpkt_ep_hash_index} - ${_cpkt_ep_url_start}")
if(_cpkt_ep_url_count LESS 1)
message(FATAL_ERROR "cpkt_cached_external_project_add requires at least one URL")
endif()
list(SUBLIST _cpkt_ep_args ${_cpkt_ep_url_start} ${_cpkt_ep_url_count} _cpkt_ep_urls)
math(EXPR _cpkt_ep_hash_value_index "${_cpkt_ep_hash_index} + 1")
list(GET _cpkt_ep_args ${_cpkt_ep_hash_value_index} _cpkt_ep_hash)
string(REGEX REPLACE "^SHA256=" "" _cpkt_ep_sha256 "${_cpkt_ep_hash}")
string(LENGTH "${_cpkt_ep_sha256}" _cpkt_ep_sha256_length)
if(NOT _cpkt_ep_hash MATCHES "^SHA256="
      OR NOT _cpkt_ep_sha256_length EQUAL 64
      OR NOT "${_cpkt_ep_sha256}" MATCHES "^[A-Fa-f0-9]+$")
message(FATAL_ERROR "cpkt_cached_external_project_add requires URL_HASH SHA256=<digest>")
endif()
list(FIND _cpkt_ep_args "DOWNLOAD_NAME" _cpkt_ep_download_name_index)
if(_cpkt_ep_download_name_index LESS 0)
list(GET _cpkt_ep_urls 0 _cpkt_ep_name_url)
string(REGEX REPLACE "[?#].*$" "" _cpkt_ep_name_url "${_cpkt_ep_name_url}")
get_filename_component(_cpkt_ep_archive_name "${_cpkt_ep_name_url}" NAME)
else()
math(EXPR _cpkt_ep_download_name_value_index "${_cpkt_ep_download_name_index} + 1")
list(GET _cpkt_ep_args ${_cpkt_ep_download_name_value_index} _cpkt_ep_archive_name)
endif()
if(_cpkt_ep_archive_name STREQUAL "")
message(FATAL_ERROR "cpkt_cached_external_project_add could not determine an archive name")
endif()
cpkt_acquire_dependency_archive(_cpkt_ep_cached_archive
    NAME "${_cpkt_ep_archive_name}"
    SHA256 "${_cpkt_ep_sha256}"
    URLS ${_cpkt_ep_urls}
    SEED_PATHS "${CPKT_DOWNLOAD_ROOT}/${_cpkt_ep_archive_name}")
math(EXPR _cpkt_ep_remove_count "${_cpkt_ep_url_count} + 1")
foreach(_cpkt_ep_remove_index RANGE 1 ${_cpkt_ep_remove_count})
list(REMOVE_AT _cpkt_ep_args ${_cpkt_ep_url_index})
endforeach()
list(INSERT _cpkt_ep_args ${_cpkt_ep_url_index} URL "${_cpkt_ep_cached_archive}")
ExternalProject_Add(${_cpkt_ep_args})
endmacro()
function(cpkt_order_shared_install shared_project static_project)
  # Both variants write common package metadata into one install prefix.
  # CMP0114 NEW makes each step target own its command. Keep the shared build
  # as a separately schedulable prerequisite of the shared install.
  ExternalProject_Add_StepTargets(${static_project} install)
  ExternalProject_Add_StepTargets(${shared_project} build install)
  ExternalProject_Add_StepDependencies(${shared_project} install
    ${static_project}-install)
endfunction()

function(cpkt_record_dependency_target target_name)
set_property(GLOBAL APPEND PROPERTY CPKT_DEPENDENCY_TARGETS "${target_name}")
endfunction()
function(cpkt_require_dependency_file path label)
if(NOT EXISTS "${path}")
message(FATAL_ERROR
      "${label} was not found at ${path}\n"
      "Provide the dependency tree for this preset under CPKT_EXTERNAL_ROOT.\n"
      "You can prebuild it explicitly with scripts/deps.sh, but normal build/test/release entry points do not do that for you.")
endif()
endfunction()
function(cpkt_normalize_prefix var path)
file(TO_CMAKE_PATH "${path}" _normalized)
set(${var} "${_normalized}" PARENT_SCOPE)
endfunction()
function(cpkt_get_target_triple out_var)
string(TOLOWER "${CPKT_TARGET_OS}" _cpkt_target_os_lower)
if(_cpkt_target_os_lower STREQUAL "darwin")
if(CPKT_TARGET_ARCH STREQUAL "arm64")
set(_triple "aarch64-apple-darwin")
else()
message(FATAL_ERROR "Unsupported Darwin CPKT_TARGET_ARCH: ${CPKT_TARGET_ARCH}")
endif()
elseif(CPKT_TARGET_ARCH STREQUAL "x86_64")
if(CPKT_TARGET_LIBC STREQUAL "musl")
set(_triple "x86_64-linux-musl")
else()
set(_triple "x86_64-linux-gnu")
endif()
elseif(CPKT_TARGET_ARCH STREQUAL "aarch64")
if(CPKT_TARGET_LIBC STREQUAL "musl")
set(_triple "aarch64-linux-musl")
else()
set(_triple "aarch64-linux-gnu")
endif()
elseif(CPKT_TARGET_ARCH STREQUAL "armhf")
if(CPKT_TARGET_LIBC STREQUAL "musl")
set(_triple "arm-linux-musleabihf")
else()
set(_triple "arm-linux-gnueabihf")
endif()
else()
message(FATAL_ERROR "Unsupported CPKT_TARGET_ARCH: ${CPKT_TARGET_ARCH}")
endif()
set(${out_var} "${_triple}" PARENT_SCOPE)
endfunction()
function(cpkt_get_openssl_config_target out_var)
string(TOLOWER "${CPKT_TARGET_OS}" _cpkt_target_os_lower)
if(_cpkt_target_os_lower STREQUAL "darwin")
if(CPKT_TARGET_ARCH STREQUAL "arm64")
set(_target "darwin64-arm64")
else()
message(FATAL_ERROR "Unsupported Darwin CPKT_TARGET_ARCH for OpenSSL: ${CPKT_TARGET_ARCH}")
endif()
elseif(CPKT_TARGET_ARCH STREQUAL "x86_64")
set(_target "linux-x86_64")
elseif(CPKT_TARGET_ARCH STREQUAL "aarch64")
set(_target "linux-aarch64")
elseif(CPKT_TARGET_ARCH STREQUAL "armhf")
set(_target "linux-armv4")
else()
message(FATAL_ERROR "Unsupported CPKT_TARGET_ARCH for OpenSSL: ${CPKT_TARGET_ARCH}")
endif()
set(${out_var} "${_target}" PARENT_SCOPE)
endfunction()
function(cpkt_get_external_c_flags out_var)
set(_flags "-O2 -DNDEBUG -g0")
if(CMAKE_C_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
string(APPEND _flags
      " -fmacro-prefix-map=${CPKT_DEPENDENCY_BUILD_ROOT}=deps-build"
      " -fmacro-prefix-map=${CPKT_EXTERNAL_ROOT}=deps"
    )
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
string(APPEND _flags " -include stdint.h -include sys/types.h")
if(CPKT_MACOS_DEPLOYMENT_TARGET)
string(APPEND _flags
        " -mmacosx-version-min=${CPKT_MACOS_DEPLOYMENT_TARGET}")
endif()
endif()
if(NOT "${CMAKE_C_FLAGS}" STREQUAL "")
set(_flags "${CMAKE_C_FLAGS} ${_flags}")
endif()
string(STRIP "${_flags}" _flags)
set(${out_var} "${_flags}" PARENT_SCOPE)
endfunction()
function(cpkt_append_external_pkg_config_env_args out_var)
set(_args ${${out_var}})
set(_pkg_config_dirs "")
foreach(_prefix_var IN ITEMS
      CPKT_ZLIB_PREFIX
      CPKT_OPENSSL_static_PREFIX
      CPKT_OPENSSL_shared_PREFIX
      CPKT_NGHTTP2_static_PREFIX
      CPKT_NGHTTP2_shared_PREFIX
      CPKT_LIBSSH2_PREFIX
      CPKT_LIBXML2_PREFIX
      CPKT_LUA_PREFIX
      CPKT_MQTTC_PREFIX
      CPKT_OPEN62541_PREFIX
      CPKT_KRB5_PREFIX
      CPKT_CYRUS_SASL_PREFIX
      CPKT_OPENLDAP_PREFIX
      CPKT_POSTGRESQL_PREFIX)
if(DEFINED ${_prefix_var} AND NOT "${${_prefix_var}}" STREQUAL "")
list(APPEND _pkg_config_dirs
        "${${_prefix_var}}/lib/pkgconfig"
        "${${_prefix_var}}/share/pkgconfig")
endif()
endforeach()
if(_pkg_config_dirs)
list(REMOVE_DUPLICATES _pkg_config_dirs)
string(JOIN ":" _pkg_config_libdir ${_pkg_config_dirs})
else()
set(_pkg_config_libdir "${CPKT_EXTERNAL_ROOT}/.pkgconfig-empty")
endif()
list(APPEND _args
    PKG_CONFIG_PATH=
    PKG_CONFIG_DIR=
    PKG_CONFIG_LIBDIR=${_pkg_config_libdir})
set(${out_var} "${_args}" PARENT_SCOPE)
endfunction()
function(cpkt_append_darwin_external_env_args out_var)
set(_args ${${out_var}})
cpkt_append_external_pkg_config_env_args(_args)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
foreach(_cpkt_required_var IN ITEMS
        CMAKE_C_COMPILER
        CMAKE_CXX_COMPILER
        CMAKE_AR
        CMAKE_RANLIB
        CMAKE_STRIP
        CMAKE_NM)
if(NOT DEFINED ${_cpkt_required_var}
          OR "${${_cpkt_required_var}}" STREQUAL "")
message(FATAL_ERROR
          "Darwin external builds require ${_cpkt_required_var}")
endif()
endforeach()
list(APPEND _args
      CC=${CMAKE_C_COMPILER}
      CXX=${CMAKE_CXX_COMPILER}
      AR=${CMAKE_AR}
      RANLIB=${CMAKE_RANLIB}
      STRIP=${CMAKE_STRIP}
      NM=${CMAKE_NM}
    )
if(CMAKE_OSX_SYSROOT)
list(APPEND _args "SDKROOT=${CMAKE_OSX_SYSROOT}")
endif()
if(CPKT_MACOS_DEPLOYMENT_TARGET)
list(APPEND _args
        MACOSX_DEPLOYMENT_TARGET=${CPKT_MACOS_DEPLOYMENT_TARGET})
endif()
if(CPKT_OSXCROSS_ROOT)
list(APPEND _args
        PATH=${CPKT_OSXCROSS_BIN_DIR}:$ENV{PATH}
        LD_LIBRARY_PATH=${CPKT_OSXCROSS_ROOT}/lib:$ENV{LD_LIBRARY_PATH})
endif()
if(CPKT_OSXCROSS_ROOT AND CMAKE_LINKER)
list(APPEND _args LDFLAGS=--ld-path=${CMAKE_LINKER})
endif()
endif()
set(${out_var} "${_args}" PARENT_SCOPE)
endfunction()
function(cpkt_append_pinned_external_toolchain_env_args out_var)
set(_args ${${out_var}})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CPKT_BUILD_DEPENDENCIES)
foreach(_cpkt_required_var IN ITEMS
        CPKT_TOOLCHAIN_ROOT
        CMAKE_C_COMPILER
        CMAKE_CXX_COMPILER
        CMAKE_LINKER
        CMAKE_AR
        CMAKE_RANLIB
        CMAKE_STRIP
        CMAKE_NM
        CMAKE_OBJCOPY
        CMAKE_OBJDUMP
        CMAKE_ADDR2LINE
        CMAKE_READELF)
if(NOT DEFINED ${_cpkt_required_var}
          OR "${${_cpkt_required_var}}" STREQUAL "")
message(FATAL_ERROR
          "Pinned Linux external builds require ${_cpkt_required_var}")
endif()
endforeach()
set(_cpkt_toolchain_bin "${CPKT_TOOLCHAIN_ROOT}/bin")
if(NOT IS_DIRECTORY "${_cpkt_toolchain_bin}")
message(FATAL_ERROR
        "Pinned Linux toolchain bin directory is missing: ${_cpkt_toolchain_bin}")
endif()
list(APPEND _args
      PATH=${_cpkt_toolchain_bin}:$ENV{PATH}
      CC=${CMAKE_C_COMPILER}
      CXX=${CMAKE_CXX_COMPILER}
      LD=${CMAKE_LINKER}
      AR=${CMAKE_AR}
      RANLIB=${CMAKE_RANLIB}
      STRIP=${CMAKE_STRIP}
      NM=${CMAKE_NM}
      OBJCOPY=${CMAKE_OBJCOPY}
      OBJDUMP=${CMAKE_OBJDUMP}
      ADDR2LINE=${CMAKE_ADDR2LINE}
      READELF=${CMAKE_READELF})
endif()
cpkt_append_darwin_external_env_args(_args)
set(${out_var} "${_args}" PARENT_SCOPE)
endfunction()
function(cpkt_get_external_cmake_step_commands build_out_var install_out_var install_dir)
set(_build_command ${CMAKE_COMMAND} --build . --parallel ${CPKT_DEPENDENCY_BUILD_JOBS})
set(_install_command ${CMAKE_COMMAND} --install . --prefix "${install_dir}")
set(_env_args "")
cpkt_append_pinned_external_toolchain_env_args(_env_args)
if(_env_args)
set(_build_command ${CMAKE_COMMAND} -E env ${_env_args} ${_build_command})
set(_install_command ${CMAKE_COMMAND} -E env ${_env_args} ${_install_command})
endif()
set(${build_out_var} "${_build_command}" PARENT_SCOPE)
set(${install_out_var} "${_install_command}" PARENT_SCOPE)
endfunction()
function(cpkt_get_external_cmake_configure_command out_var)
set(_configure_command ${CMAKE_COMMAND} -S <SOURCE_DIR> -B <BINARY_DIR>)
if(CMAKE_GENERATOR)
list(APPEND _configure_command -G "${CMAKE_GENERATOR}")
endif()
set(_env_args "")
cpkt_append_pinned_external_toolchain_env_args(_env_args)
if(_env_args)
set(_configure_command ${CMAKE_COMMAND} -E env ${_env_args} ${_configure_command})
endif()
set(${out_var} "${_configure_command}" PARENT_SCOPE)
endfunction()
function(cpkt_get_strip_dependency_install_command out_var install_dir)
if(NOT CMAKE_STRIP)
message(FATAL_ERROR "CMAKE_STRIP is required when building release dependencies")
endif()
set(_strip_static_archives ON)
set(_strip_shared_libraries ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(_strip_static_archives OFF)
set(_strip_shared_libraries OFF)
endif()
set(_command
    ${CMAKE_COMMAND}
      -DCPKT_STRIP_BIN=${CMAKE_STRIP}
      -DCPKT_STRIP_ROOT=${install_dir}
      -DCPKT_STRIP_STATIC_ARCHIVES=${_strip_static_archives}
      -DCPKT_STRIP_SHARED_LIBRARIES=${_strip_shared_libraries}
      -P ${CMAKE_SOURCE_DIR}/cmake/strip_dependency_install_tree.cmake
  )
set(${out_var} "${_command}" PARENT_SCOPE)
endfunction()
function(cpkt_get_autotools_link_flags out_var)
set(_flags "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
set(_flags "-Wl,--enable-new-dtags,-rpath,\\\\$$ORIGIN")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(_flags "-Wl,-rpath,@loader_path")
if(NOT CMAKE_CROSSCOMPILING)
string(APPEND _flags " -Wl,-not_for_dyld_shared_cache")
endif()
endif()
if(NOT "${CMAKE_SHARED_LINKER_FLAGS}" STREQUAL "")
string(APPEND _flags " ${CMAKE_SHARED_LINKER_FLAGS}")
endif()
string(STRIP "${_flags}" _flags)
set(${out_var} "${_flags}" PARENT_SCOPE)
endfunction()
function(cpkt_append_common_external_cmake_args out_var)
set(_args
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_LINKER=${CMAKE_LINKER}
    -DCMAKE_AR=${CMAKE_AR}
    -DCMAKE_RANLIB=${CMAKE_RANLIB}
    -DCMAKE_STRIP=${CMAKE_STRIP}
    -DCMAKE_NM=${CMAKE_NM}
    -DCMAKE_OBJCOPY=${CMAKE_OBJCOPY}
    -DCMAKE_OBJDUMP=${CMAKE_OBJDUMP}
    -DCMAKE_ADDR2LINE=${CMAKE_ADDR2LINE}
    -DCMAKE_READELF=${CMAKE_READELF}
    -Wno-dev
  )
if(CMAKE_TOOLCHAIN_FILE)
list(APPEND _args -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()
if(CMAKE_CROSSCOMPILING)
list(APPEND _args -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
list(APPEND _args
      "-DCMAKE_OSX_SYSROOT:PATH=${CMAKE_OSX_SYSROOT}"
      -DCMAKE_OSX_DEPLOYMENT_TARGET=${CPKT_MACOS_DEPLOYMENT_TARGET}
      -DCMAKE_INSTALL_NAME_DIR=@rpath
      -DCMAKE_BUILD_WITH_INSTALL_NAME_DIR=ON
      -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON)
if(CMAKE_CROSSCOMPILING)
list(APPEND _args
      -DCPKT_OSXCROSS_HOST=${CPKT_OSXCROSS_HOST}
      -DCPKT_MACOS_DEPLOYMENT_TARGET=${CPKT_MACOS_DEPLOYMENT_TARGET})
endif()
endif()
cpkt_get_external_c_flags(_cpkt_external_c_flags)
list(APPEND _args -DCMAKE_C_FLAGS=${_cpkt_external_c_flags})
set(${out_var} "${_args}" PARENT_SCOPE)
endfunction()
include("${CMAKE_CURRENT_LIST_DIR}/dependencies/openssl.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/dependencies/nghttp2.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/dependencies/zlib.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/dependencies/libssh2.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/dependencies/curl.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/dependencies/libxml2.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/dependencies/lua.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/dependencies/mqttc.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/dependencies/krb5.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/dependencies/cyrus-sasl.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/dependencies/openldap.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/dependencies/cmocka.cmake")

function(cpkt_configure_dependencies)
cpkt_prepare_dependency_component(
    NAME openssl
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/openssl"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/openssl/install"
    VARIABLES CPKT_OPENSSL_VERSION CPKT_OPENSSL_BUILD_CONFIG_REVISION
    INPUT_FILES
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CpktDependencies.cmake"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies/openssl.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/patch_openssl_buildinfo.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/patch_darwin_generated_install_names.cmake")
cpkt_prepare_dependency_component(
    NAME zlib
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/zlib"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/zlib/install"
    VARIABLES CPKT_ZLIB_VERSION
    INPUT_FILES
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CpktDependencies.cmake"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies/zlib.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/patch_zlib_single_pass.cmake")
cpkt_prepare_dependency_component(
    NAME nghttp2
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/nghttp2"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/nghttp2/install"
    VARIABLES CPKT_NGHTTP2_VERSION
    INPUT_FILES
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CpktDependencies.cmake"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies/nghttp2.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake")
cpkt_prepare_dependency_component(
    NAME libssh2
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/libssh2"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/libssh2/install"
    VARIABLES CPKT_LIBSSH2_VERSION
    DEPENDS openssl zlib
    INPUT_FILES
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CpktDependencies.cmake"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies/libssh2.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/patch_libssh2_single_pass.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/patch_libssh2_poll_elapsed.cmake")
cpkt_prepare_dependency_component(
    NAME curl
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/curl"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/curl/install"
    VARIABLES CPKT_CURL_VERSION
    DEPENDS zlib openssl nghttp2 libssh2
    INPUT_FILES
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CpktDependencies.cmake"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies/curl.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake")
cpkt_prepare_dependency_component(
    NAME libxml2
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/libxml2"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/libxml2/install"
    VARIABLES CPKT_LIBXML2_VERSION
    DEPENDS zlib
    INPUT_FILES
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CpktDependencies.cmake"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies/libxml2.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake")
cpkt_prepare_dependency_component(
    NAME lua
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/lua"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/lua/install"
    VARIABLES CPKT_LUA_VERSION
    INPUT_FILES
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CpktDependencies.cmake"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies/lua.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/install_lua.cmake")
cpkt_prepare_dependency_component(
    NAME mqttc
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/mqtt-c"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/mqtt-c/install"
    VARIABLES CPKT_MQTTC_COMMIT
    INPUT_FILES
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CpktDependencies.cmake"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies/mqttc.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake")
cpkt_prepare_dependency_component(
    NAME krb5
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/krb5"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/krb5/install"
    VARIABLES CPKT_KRB5_VERSION CPKT_DARWIN_HOST_MIG_REVISION
    DEPENDS openssl
    INPUT_FILES
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CpktDependencies.cmake"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies/krb5.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/assert_krb5_features.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/patches/krb5.series"
      "${CMAKE_SOURCE_DIR}/cmake/patches/krb5_const_correctness.patch"
      "${CMAKE_SOURCE_DIR}/cmake/patches/krb5_tls_bundle.patch"
      "${CMAKE_SOURCE_DIR}/cmake/patches/krb5_error_va_list.patch"
      "${CMAKE_SOURCE_DIR}/cmake/patches/krb5_gss_trace_callback.patch")
cpkt_prepare_dependency_component(
    NAME cyrus-sasl
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/cyrus-sasl"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/cyrus-sasl/install"
    VARIABLES CPKT_CYRUS_SASL_VERSION
    DEPENDS krb5 openssl
    INPUT_FILES
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CpktDependencies.cmake"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies/cyrus-sasl.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/cyrus_sasl_md5global.h.in"
      "${CMAKE_SOURCE_DIR}/cmake/patches/cyrus_sasl_relocatable_plugins.patch"
      "${CMAKE_SOURCE_DIR}/cmake/patches/cyrus_sasl_build_warnings.patch"
      "${CMAKE_SOURCE_DIR}/cmake/patches/cyrus_sasl.series"
      "${CMAKE_SOURCE_DIR}/cmake/apply_patch_series.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/assert_cyrus_sasl_gssapi.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/enable_cyrus_sasl_static_gs2.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/assert_cyrus_sasl_plugins.cmake")
cpkt_prepare_dependency_component(
    NAME openldap
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/openldap"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/openldap/install"
    VARIABLES CPKT_OPENLDAP_VERSION
    DEPENDS cyrus-sasl openssl krb5
    INPUT_FILES
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CpktDependencies.cmake"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies/openldap.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/build_openldap_libraries.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/patch_openldap_lutil_link.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/remove_static_archive_member.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/patches/openldap.series"
      "${CMAKE_SOURCE_DIR}/cmake/patches/openldap_client_const.patch"
      "${CMAKE_SOURCE_DIR}/cmake/patches/openldap_sasl_logging.patch"
      "${CMAKE_SOURCE_DIR}/cmake/patches/openldap_c89_log_setter.patch")
cpkt_prepare_dependency_component(
    NAME cmocka
    BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/cmocka"
    INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/cmocka/install"
    VARIABLES CPKT_CMOCKA_VERSION
    INPUT_FILES
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CpktDependencies.cmake"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies/cmocka.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake")
set(_all_dependency_targets "")
foreach(_component IN LISTS CPKT_ACTIVE_COMPONENTS)
string(JSON _owner GET "${CPKT_INVENTORY}" components "${_component}" group)
string(REPLACE "-" "_" _function "cpkt_add_${_component}")
set(CPKT_BUILD_DEPENDENCIES OFF)
if(CPKT_DEPENDENCY_PRODUCER AND _owner STREQUAL CPKT_GROUP)
set(CPKT_BUILD_DEPENDENCIES ON)
endif()
get_property(_before GLOBAL PROPERTY CPKT_DEPENDENCY_TARGETS)
cmake_language(CALL "${_function}")
get_property(_after GLOBAL PROPERTY CPKT_DEPENDENCY_TARGETS)
set(_component_targets "${_after}")
if(_before)
list(REMOVE_ITEM _component_targets ${_before})
endif()
set_property(GLOBAL PROPERTY "CPKT_PRODUCER_TARGETS_${_component}" "${_component_targets}")
if(_component_targets)
set(_remaining "${_component_targets}")
list(POP_BACK _remaining _last_project)
get_property(_complete GLOBAL PROPERTY "CPKT_COMPONENT_COMPLETE_${_component}")
set(_install_stamps "")
foreach(_project IN LISTS _remaining)
  ExternalProject_Get_Property(${_project} STAMP_DIR)
  list(APPEND _install_stamps "${STAMP_DIR}/${_project}-done")
endforeach()
ExternalProject_Add_Step(${_last_project} component-complete
  COMMAND "${CMAKE_COMMAND}" -E touch "${_complete}"
  DEPENDEES install DEPENDS ${_install_stamps}
  BYPRODUCTS "${_complete}")

add_custom_target(cpkt_deps_${_component} DEPENDS ${_component_targets})
list(APPEND _all_dependency_targets ${_component_targets})
endif()
endforeach()
if(CPKT_DEPENDENCY_PRODUCER)
add_custom_target(cpkt_deps_all DEPENDS ${_all_dependency_targets})
add_custom_target(cpkt_deps DEPENDS cpkt_deps_all)
endif()
endfunction()
