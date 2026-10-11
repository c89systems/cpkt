# Existing supported static/shared discovery and platform closures.
function(cpkt_metadata_file operation)
  set(args ${ARGN})
  if(operation STREQUAL "WRITE" OR operation STREQUAL "MAKE_DIRECTORY")
    list(GET args 0 path)
    string(REGEX MATCH "/lib/cmake/([^/]+)" matched "${path}")
    if(matched AND NOT CMAKE_MATCH_1 IN_LIST CPKT_METADATA_CMAKE)
      return()
    endif()
    string(REGEX MATCH "/lib/pkgconfig/([^/]+)\\.pc$" matched "${path}")
    if(matched AND NOT CMAKE_MATCH_1 IN_LIST CPKT_METADATA_PC)
      return()
    endif()
  endif()
  if(operation STREQUAL "WRITE")
    # Expanding ARGN as a list destroys semicolons in link interfaces.
    set(content "")
    math(EXPR last "${ARGC} - 1")
    foreach(index RANGE 2 ${last})
      string(APPEND content "${ARGV${index}}")
    endforeach()
    file(WRITE "${ARGV1}" "${content}")
  elseif(operation STREQUAL "MAKE_DIRECTORY")
    file(MAKE_DIRECTORY ${ARGN})
  else()
    message(FATAL_ERROR "Unsupported metadata file operation: ${operation}")
  endif()
endfunction()
set(_cpkt_static_library_suffix ".a")
if(CPKT_TARGET_ID STREQUAL "arm64-apple-darwin")
  set(_cpkt_shared_library_suffix ".dylib")
  set(_cpkt_lua_facade_shared_library_link_name "libcpkt_lua.dylib")
  set(_cpkt_lua_facade_shared_library_abi_name "libcpkt_lua.${CPKT_LUA_ABI_VERSION}.dylib")
  set(_cpkt_lua_facade_shared_library_real_name "libcpkt_lua.${CPKT_BUNDLE_VERSION}.dylib")
  set(_cpkt_lua_runtime_shared_library_link_name "libcpkt_lua_runtime.dylib")
  set(_cpkt_lua_runtime_shared_library_abi_name "libcpkt_lua_runtime.${CPKT_LUA_RUNTIME_ABI_VERSION}.dylib")
  set(_cpkt_lua_runtime_shared_library_real_name "libcpkt_lua_runtime.${CPKT_BUNDLE_VERSION}.dylib")
  set(_cpkt_openssl_shared_library_link_name "libcpkt_openssl.dylib")
  set(_cpkt_openssl_shared_library_abi_name "libcpkt_openssl.${CPKT_OPENSSL_ABI_VERSION}.dylib")
  set(_cpkt_openssl_shared_library_real_name "libcpkt_openssl.${CPKT_BUNDLE_VERSION}.dylib")
  set(_cpkt_nghttp2_shared_library_link_name "libcpkt_nghttp2.dylib")
  set(_cpkt_nghttp2_shared_library_abi_name "libcpkt_nghttp2.${CPKT_NGHTTP2_ABI_VERSION}.dylib")
  set(_cpkt_nghttp2_shared_library_real_name "libcpkt_nghttp2.${CPKT_BUNDLE_VERSION}.dylib")
  set(_cpkt_libssh2_facade_shared_library_link_name "libcpkt_libssh2.dylib")
  set(_cpkt_libssh2_facade_shared_library_abi_name "libcpkt_libssh2.${CPKT_LIBSSH2_ABI_VERSION}.dylib")
  set(_cpkt_libssh2_facade_shared_library_real_name "libcpkt_libssh2.${CPKT_BUNDLE_VERSION}.dylib")
  set(_cpkt_mqttc_facade_shared_library_link_name "libcpkt_mqttc.dylib")
  set(_cpkt_mqttc_facade_shared_library_abi_name "libcpkt_mqttc.${CPKT_MQTTC_ABI_VERSION}.dylib")
  set(_cpkt_mqttc_facade_shared_library_real_name "libcpkt_mqttc.${CPKT_BUNDLE_VERSION}.dylib")
  set(_cpkt_gssapi_shared_library_link_name "libcpkt_gssapi.dylib")
  set(_cpkt_gssapi_shared_library_abi_name "libcpkt_gssapi.${CPKT_GSSAPI_ABI_VERSION}.dylib")
  set(_cpkt_gssapi_shared_library_real_name "libcpkt_gssapi.${CPKT_BUNDLE_VERSION}.dylib")
  set(_cpkt_sasl_shared_library_link_name "libcpkt_sasl.dylib")
  set(_cpkt_sasl_shared_library_abi_name "libcpkt_sasl.${CPKT_SASL_ABI_VERSION}.dylib")
  set(_cpkt_sasl_shared_library_real_name "libcpkt_sasl.${CPKT_BUNDLE_VERSION}.dylib")
  set(_cpkt_libssh2_shared_library_name "libssh2.1.dylib")
  set(_cpkt_libxml2_shared_library_name "libxml2.dylib")
  set(_cpkt_lua_shared_library_name "liblua.dylib")
  set(_cpkt_mqttc_shared_library_name "libmqttc.dylib")
else()
  set(_cpkt_shared_library_suffix ".so")
  set(_cpkt_lua_facade_shared_library_link_name "libcpkt_lua.so")
  set(_cpkt_lua_facade_shared_library_abi_name "libcpkt_lua.so.${CPKT_LUA_ABI_VERSION}")
  set(_cpkt_lua_facade_shared_library_real_name "libcpkt_lua.so.${CPKT_BUNDLE_VERSION}")
  set(_cpkt_lua_runtime_shared_library_link_name "libcpkt_lua_runtime.so")
  set(_cpkt_lua_runtime_shared_library_abi_name "libcpkt_lua_runtime.so.${CPKT_LUA_RUNTIME_ABI_VERSION}")
  set(_cpkt_lua_runtime_shared_library_real_name "libcpkt_lua_runtime.so.${CPKT_BUNDLE_VERSION}")
  set(_cpkt_openssl_shared_library_link_name "libcpkt_openssl.so")
  set(_cpkt_openssl_shared_library_abi_name "libcpkt_openssl.so.${CPKT_OPENSSL_ABI_VERSION}")
  set(_cpkt_openssl_shared_library_real_name "libcpkt_openssl.so.${CPKT_BUNDLE_VERSION}")
  set(_cpkt_nghttp2_shared_library_link_name "libcpkt_nghttp2.so")
  set(_cpkt_nghttp2_shared_library_abi_name "libcpkt_nghttp2.so.${CPKT_NGHTTP2_ABI_VERSION}")
  set(_cpkt_nghttp2_shared_library_real_name "libcpkt_nghttp2.so.${CPKT_BUNDLE_VERSION}")
  set(_cpkt_libssh2_facade_shared_library_link_name "libcpkt_libssh2.so")
  set(_cpkt_libssh2_facade_shared_library_abi_name "libcpkt_libssh2.so.${CPKT_LIBSSH2_ABI_VERSION}")
  set(_cpkt_libssh2_facade_shared_library_real_name "libcpkt_libssh2.so.${CPKT_BUNDLE_VERSION}")
  set(_cpkt_mqttc_facade_shared_library_link_name "libcpkt_mqttc.so")
  set(_cpkt_mqttc_facade_shared_library_abi_name "libcpkt_mqttc.so.${CPKT_MQTTC_ABI_VERSION}")
  set(_cpkt_mqttc_facade_shared_library_real_name "libcpkt_mqttc.so.${CPKT_BUNDLE_VERSION}")
  set(_cpkt_gssapi_shared_library_link_name "libcpkt_gssapi.so")
  set(_cpkt_gssapi_shared_library_abi_name "libcpkt_gssapi.so.${CPKT_GSSAPI_ABI_VERSION}")
  set(_cpkt_gssapi_shared_library_real_name "libcpkt_gssapi.so.${CPKT_BUNDLE_VERSION}")
  set(_cpkt_sasl_shared_library_link_name "libcpkt_sasl.so")
  set(_cpkt_sasl_shared_library_abi_name "libcpkt_sasl.so.${CPKT_SASL_ABI_VERSION}")
  set(_cpkt_sasl_shared_library_real_name "libcpkt_sasl.so.${CPKT_BUNDLE_VERSION}")
  set(_cpkt_libssh2_shared_library_name "libssh2.so")
  set(_cpkt_libxml2_shared_library_name "libxml2.so")
  set(_cpkt_lua_shared_library_name "liblua.so")
  set(_cpkt_mqttc_shared_library_name "libmqttc.so")
endif()
function(cpkt_write_config_version package_dir config_stem package_version)
  if(NOT package_dir IN_LIST CPKT_METADATA_CMAKE)
    return()
  endif()
  cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/${package_dir}")
  cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/${package_dir}/${config_stem}ConfigVersion.cmake"
    "set(PACKAGE_VERSION \"${package_version}\")\n"
    "if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)\n"
    "  set(PACKAGE_VERSION_EXACT TRUE)\n"
    "endif()\n"
    "if(PACKAGE_FIND_VERSION VERSION_LESS_EQUAL PACKAGE_VERSION)\n"
    "  set(PACKAGE_VERSION_COMPATIBLE TRUE)\n"
    "endif()\n"
  )
endfunction()

set(_cpkt_armhf_static_extra_libs "")
set(_cpkt_armhf_static_extra_pc_libs "")
if(CPKT_TARGET_ID MATCHES "^armhf-")
  set(_cpkt_armhf_static_extra_libs "atomic")
  set(_cpkt_armhf_static_extra_pc_libs "-latomic")
endif()

set(_cpkt_openssl_static_private_pc_libs "")
if(CPKT_TARGET_ID MATCHES "-linux-")
  set(_cpkt_openssl_static_private_pc_libs "-ldl -pthread ${_cpkt_armhf_static_extra_pc_libs}")
endif()
set(_cpkt_libxml2_static_private_pc_libs "-lm")
set(_cpkt_libxml2_static_iconv_pc_libs "")
set(_cpkt_libxml2_static_iconv_cmake_libs "Iconv::Iconv")
set(_cpkt_libxml2_shared_iconv_cmake_libs "Iconv::Iconv")
set(_cpkt_lua_static_private_pc_libs "-lm")
if(CPKT_TARGET_ID MATCHES "-linux-")
  set(_cpkt_libxml2_static_private_pc_libs "-ldl -lm -pthread")
  set(_cpkt_lua_static_private_pc_libs "-lm -ldl")
elseif(CPKT_TARGET_ID STREQUAL "arm64-apple-darwin")
  set(_cpkt_libxml2_static_iconv_pc_libs "-liconv")
  set(_cpkt_libxml2_static_iconv_cmake_libs "Iconv::Iconv;iconv")
  set(_cpkt_libxml2_shared_iconv_cmake_libs "Iconv::Iconv;iconv")
endif()
set(_cpkt_libxml2_static_private_pc_libs_full "${_cpkt_libxml2_static_private_pc_libs}")
if(_cpkt_libxml2_static_iconv_pc_libs)
  string(APPEND _cpkt_libxml2_static_private_pc_libs_full " ${_cpkt_libxml2_static_iconv_pc_libs}")
endif()

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/OpenSSL")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/OpenSSL/OpenSSLConfig.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "find_dependency(Threads REQUIRED)\n"
  "get_filename_component(_cpkt_openssl_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "set(OPENSSL_FOUND TRUE)\n"
  "set(OPENSSL_INCLUDE_DIR \"\${_cpkt_openssl_prefix}/include\")\n"
  "set(OPENSSL_VERSION \"${CPKT_OPENSSL_VERSION}\")\n"
  "set(OPENSSL_CRYPTO_LIBRARY \"\${_cpkt_openssl_prefix}/lib/libcrypto${_cpkt_static_library_suffix}\")\n"
  "set(OPENSSL_SSL_LIBRARY \"\${_cpkt_openssl_prefix}/lib/libssl${_cpkt_static_library_suffix}\")\n"
  "set(_cpkt_openssl_static_extra_libs \"${_cpkt_armhf_static_extra_libs}\")\n"
  "if(NOT TARGET OpenSSL::Crypto)\n"
  "  add_library(OpenSSL::Crypto STATIC IMPORTED)\n"
  "  set_target_properties(OpenSSL::Crypto PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${OPENSSL_CRYPTO_LIBRARY}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${OPENSSL_INCLUDE_DIR}\"\n"
  "    INTERFACE_LINK_LIBRARIES \"\${CMAKE_DL_LIBS};Threads::Threads;\${_cpkt_openssl_static_extra_libs}\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET OpenSSL::SSL)\n"
  "  add_library(OpenSSL::SSL STATIC IMPORTED)\n"
  "  set_target_properties(OpenSSL::SSL PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${OPENSSL_SSL_LIBRARY}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${OPENSSL_INCLUDE_DIR}\"\n"
  "    INTERFACE_LINK_LIBRARIES OpenSSL::Crypto\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::openssl_crypto_shared)\n"
  "  add_library(cpkt::openssl_crypto_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::openssl_crypto_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_openssl_prefix}/lib/libcrypto${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${OPENSSL_INCLUDE_DIR}\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::openssl_ssl_shared)\n"
  "  add_library(cpkt::openssl_ssl_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::openssl_ssl_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_openssl_prefix}/lib/libssl${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${OPENSSL_INCLUDE_DIR}\"\n"
  "    INTERFACE_LINK_LIBRARIES cpkt::openssl_crypto_shared\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("OpenSSL" "OpenSSL" "${CPKT_OPENSSL_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/CpktOpenSSL")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/CpktOpenSSL/CpktOpenSSLConfig.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "get_filename_component(_cpkt_openssl_facade_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "find_dependency(OpenSSL CONFIG REQUIRED)\n"
  "set(CpktOpenSSL_FOUND TRUE)\n"
  "set(CpktOpenSSL_VERSION \"${CPKT_OPENSSL_VERSION}\")\n"
  "if(NOT TARGET cpkt::openssl)\n"
  "  add_library(cpkt::openssl STATIC IMPORTED)\n"
  "  set_target_properties(cpkt::openssl PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_openssl_facade_prefix}/lib/libcpkt_openssl${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_openssl_facade_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES \"OpenSSL::SSL;OpenSSL::Crypto\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::openssl_shared)\n"
  "  add_library(cpkt::openssl_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::openssl_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_openssl_facade_prefix}/lib/libcpkt_openssl${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_openssl_facade_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES \"cpkt::openssl_ssl_shared;cpkt::openssl_crypto_shared\"\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("CpktOpenSSL" "CpktOpenSSL" "${CPKT_OPENSSL_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/CpktNghttp2")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/CpktNghttp2/CpktNghttp2Config.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "get_filename_component(_cpkt_nghttp2_facade_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "find_dependency(nghttp2 CONFIG REQUIRED)\n"
  "set(CpktNghttp2_FOUND TRUE)\n"
  "set(CpktNghttp2_VERSION \"${CPKT_NGHTTP2_VERSION}\")\n"
  "if(NOT TARGET cpkt::nghttp2)\n"
  "  add_library(cpkt::nghttp2 STATIC IMPORTED)\n"
  "  set_target_properties(cpkt::nghttp2 PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_nghttp2_facade_prefix}/lib/libcpkt_nghttp2${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_nghttp2_facade_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES nghttp2::nghttp2\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::nghttp2_facade_shared)\n"
  "  add_library(cpkt::nghttp2_facade_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::nghttp2_facade_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_nghttp2_facade_prefix}/lib/libcpkt_nghttp2${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_nghttp2_facade_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES cpkt::nghttp2_shared\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("CpktNghttp2" "CpktNghttp2" "${CPKT_NGHTTP2_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/CpktLibssh2")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/CpktLibssh2/CpktLibssh2Config.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "get_filename_component(_cpkt_libssh2_facade_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "find_dependency(Libssh2 CONFIG REQUIRED)\n"
  "set(CpktLibssh2_FOUND TRUE)\n"
  "set(CpktLibssh2_VERSION \"${CPKT_LIBSSH2_VERSION}\")\n"
  "if(NOT TARGET cpkt::libssh2)\n"
  "  add_library(cpkt::libssh2 STATIC IMPORTED)\n"
  "  set_target_properties(cpkt::libssh2 PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_libssh2_facade_prefix}/lib/libcpkt_libssh2${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_libssh2_facade_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES Libssh2::libssh2\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::libssh2_facade_shared)\n"
  "  add_library(cpkt::libssh2_facade_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::libssh2_facade_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_libssh2_facade_prefix}/lib/libcpkt_libssh2${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_libssh2_facade_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES cpkt::libssh2_shared\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("CpktLibssh2" "CpktLibssh2" "${CPKT_LIBSSH2_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/zlib")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/zlib/ZLIBConfig.cmake"
  "get_filename_component(_cpkt_zlib_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "set(ZLIB_FOUND TRUE)\n"
  "set(ZLIB_INCLUDE_DIR \"\${_cpkt_zlib_prefix}/include\")\n"
  "set(ZLIB_VERSION \"${CPKT_ZLIB_VERSION}\")\n"
  "set(ZLIB_LIBRARY \"\${_cpkt_zlib_prefix}/lib/libz${_cpkt_static_library_suffix}\")\n"
  "if(NOT TARGET ZLIB::ZLIB)\n"
  "  add_library(ZLIB::ZLIB STATIC IMPORTED)\n"
  "  set_target_properties(ZLIB::ZLIB PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${ZLIB_LIBRARY}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${ZLIB_INCLUDE_DIR}\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::zlib_shared)\n"
  "  add_library(cpkt::zlib_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::zlib_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_zlib_prefix}/lib/libz${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${ZLIB_INCLUDE_DIR}\"\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("zlib" "ZLIB" "${CPKT_ZLIB_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/nghttp2")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/nghttp2/nghttp2Config.cmake"
  "get_filename_component(_cpkt_nghttp2_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "set(nghttp2_FOUND TRUE)\n"
  "set(nghttp2_VERSION \"${CPKT_NGHTTP2_VERSION}\")\n"
  "if(NOT TARGET nghttp2::nghttp2)\n"
  "  add_library(nghttp2::nghttp2 STATIC IMPORTED)\n"
  "  set_target_properties(nghttp2::nghttp2 PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_nghttp2_prefix}/lib/libnghttp2${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_nghttp2_prefix}/include\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::nghttp2_shared)\n"
  "  add_library(cpkt::nghttp2_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::nghttp2_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_nghttp2_prefix}/lib/libnghttp2${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_nghttp2_prefix}/include\"\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("nghttp2" "nghttp2" "${CPKT_NGHTTP2_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/libssh2")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/libssh2/libssh2-config.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "get_filename_component(_cpkt_libssh2_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "find_dependency(OpenSSL CONFIG REQUIRED)\n"
  "find_dependency(ZLIB CONFIG REQUIRED)\n"
  "set(libssh2_FOUND TRUE)\n"
  "set(Libssh2_FOUND TRUE)\n"
  "set(libssh2_VERSION \"${CPKT_LIBSSH2_VERSION}\")\n"
  "set(Libssh2_VERSION \"${CPKT_LIBSSH2_VERSION}\")\n"
  "if(NOT TARGET libssh2::libssh2_static)\n"
  "  add_library(libssh2::libssh2_static STATIC IMPORTED)\n"
  "  set_target_properties(libssh2::libssh2_static PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_libssh2_prefix}/lib/libssh2${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_libssh2_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES \"OpenSSL::Crypto;ZLIB::ZLIB\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET Libssh2::libssh2)\n"
  "  add_library(Libssh2::libssh2 ALIAS libssh2::libssh2_static)\n"
  "endif()\n"
  "if(NOT TARGET cpkt::libssh2_shared)\n"
  "  add_library(cpkt::libssh2_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::libssh2_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_libssh2_prefix}/lib/${_cpkt_libssh2_shared_library_name}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_libssh2_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES \"cpkt::zlib_shared\"\n"
  "  )\n"
  "endif()\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/libssh2/libssh2-config-version.cmake"
  "set(PACKAGE_VERSION \"${CPKT_LIBSSH2_VERSION}\")\n"
  "if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)\n"
  "  set(PACKAGE_VERSION_EXACT TRUE)\n"
  "endif()\n"
  "if(PACKAGE_FIND_VERSION VERSION_LESS_EQUAL PACKAGE_VERSION)\n"
  "  set(PACKAGE_VERSION_COMPATIBLE TRUE)\n"
  "endif()\n"
)

set(_cpkt_curl_static_platform_libs "")
set(_cpkt_curl_static_platform_pc_libs "")
if(CPKT_TARGET_ID STREQUAL "arm64-apple-darwin")
  set(_cpkt_curl_static_platform_libs "-framework SystemConfiguration;-framework Security;-framework CoreFoundation;-framework CoreServices")
  set(_cpkt_curl_static_platform_pc_libs "-framework SystemConfiguration -framework Security -framework CoreFoundation -framework CoreServices")
endif()
cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/CURL")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/CURL/CURLConfig.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "get_filename_component(_cpkt_curl_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "find_dependency(OpenSSL CONFIG REQUIRED)\n"
  "find_dependency(ZLIB CONFIG REQUIRED)\n"
  "find_dependency(nghttp2 CONFIG REQUIRED)\n"
  "find_dependency(Libssh2 CONFIG REQUIRED)\n"
  "set(CURL_FOUND TRUE)\n"
  "set(CURL_VERSION \"${CPKT_CURL_VERSION}\")\n"
  "set(CURL_INCLUDE_DIRS \"\${_cpkt_curl_prefix}/include\")\n"
  "set(CURL_LIBRARIES CURL::libcurl)\n"
  "set(_cpkt_curl_static_platform_libs \"${_cpkt_curl_static_platform_libs}\")\n"
  "if(NOT TARGET CURL::libcurl)\n"
  "  add_library(CURL::libcurl STATIC IMPORTED)\n"
  "  set_target_properties(CURL::libcurl PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_curl_prefix}/lib/libcurl${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_curl_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES \"Libssh2::libssh2;nghttp2::nghttp2;OpenSSL::SSL;OpenSSL::Crypto;ZLIB::ZLIB;Threads::Threads;\${_cpkt_curl_static_platform_libs}\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::curl_shared)\n"
  "  add_library(cpkt::curl_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::curl_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_curl_prefix}/lib/libcurl${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_curl_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES \"cpkt::openssl_ssl_shared;cpkt::openssl_crypto_shared;cpkt::nghttp2_shared;cpkt::libssh2_shared;cpkt::zlib_shared\"\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("CURL" "CURL" "${CPKT_CURL_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/libxml2")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/libxml2/libxml2-config.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "get_filename_component(_cpkt_libxml2_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "find_dependency(ZLIB CONFIG REQUIRED)\n"
  "find_dependency(Iconv REQUIRED)\n"
  "find_dependency(Threads REQUIRED)\n"
  "set(libxml2_FOUND TRUE)\n"
  "set(LibXml2_FOUND TRUE)\n"
  "set(LIBXML2_FOUND TRUE)\n"
  "set(LIBXML2_INCLUDE_DIR \"\${_cpkt_libxml2_prefix}/include/libxml2\")\n"
  "set(libxml2_VERSION \"${CPKT_LIBXML2_VERSION}\")\n"
  "set(LibXml2_VERSION \"${CPKT_LIBXML2_VERSION}\")\n"
  "set(LIBXML2_VERSION_STRING \"${CPKT_LIBXML2_VERSION}\")\n"
  "set(LIBXML2_INCLUDE_DIRS \"\${LIBXML2_INCLUDE_DIR}\")\n"
  "set(LIBXML2_LIBRARIES LibXml2::LibXml2)\n"
  "if(NOT TARGET LibXml2::LibXml2)\n"
  "  add_library(LibXml2::LibXml2 STATIC IMPORTED)\n"
  "  set_target_properties(LibXml2::LibXml2 PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_libxml2_prefix}/lib/libxml2${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${LIBXML2_INCLUDE_DIR}\"\n"
  "    INTERFACE_LINK_LIBRARIES \"ZLIB::ZLIB;${_cpkt_libxml2_static_iconv_cmake_libs};Threads::Threads;\${CMAKE_DL_LIBS};m\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::libxml2_shared)\n"
  "  add_library(cpkt::libxml2_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::libxml2_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_libxml2_prefix}/lib/${_cpkt_libxml2_shared_library_name}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${LIBXML2_INCLUDE_DIR}\"\n"
  "    INTERFACE_LINK_LIBRARIES \"cpkt::zlib_shared;${_cpkt_libxml2_shared_iconv_cmake_libs}\"\n"
  "  )\n"
  "endif()\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/libxml2/libxml2-config-version.cmake"
  "set(PACKAGE_VERSION \"${CPKT_LIBXML2_VERSION}\")\n"
  "if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)\n"
  "  set(PACKAGE_VERSION_EXACT TRUE)\n"
  "endif()\n"
  "if(PACKAGE_FIND_VERSION VERSION_LESS_EQUAL PACKAGE_VERSION)\n"
  "  set(PACKAGE_VERSION_COMPATIBLE TRUE)\n"
  "endif()\n"
)

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/Lua")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/Lua/LuaConfig.cmake"
  "get_filename_component(_cpkt_lua_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "set(Lua_FOUND TRUE)\n"
  "set(LUA_FOUND TRUE)\n"
  "set(LUA_INCLUDE_DIR \"\${_cpkt_lua_prefix}/include\")\n"
  "set(Lua_VERSION \"${CPKT_LUA_VERSION}\")\n"
  "set(LUA_VERSION_STRING \"${CPKT_LUA_VERSION}\")\n"
  "set(LUA_INCLUDE_DIRS \"\${LUA_INCLUDE_DIR}\")\n"
  "set(LUA_LIBRARIES Lua::Lua)\n"
  "if(NOT TARGET Lua::Lua)\n"
  "  add_library(Lua::Lua STATIC IMPORTED)\n"
  "  set_target_properties(Lua::Lua PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_lua_prefix}/lib/liblua${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${LUA_INCLUDE_DIR}\"\n"
  "    INTERFACE_LINK_LIBRARIES \"m;\${CMAKE_DL_LIBS}\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::lua_shared)\n"
  "  add_library(cpkt::lua_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::lua_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_lua_prefix}/lib/${_cpkt_lua_shared_library_name}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${LUA_INCLUDE_DIR}\"\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("Lua" "Lua" "${CPKT_LUA_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/CpktLua")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/CpktLua/CpktLuaConfig.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "get_filename_component(_cpkt_lua_facade_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "find_dependency(Lua CONFIG REQUIRED)\n"
  "set(CpktLua_FOUND TRUE)\n"
  "set(CpktLua_VERSION \"${CPKT_LUA_VERSION}\")\n"
  "if(NOT TARGET cpkt::lua)\n"
  "  add_library(cpkt::lua STATIC IMPORTED)\n"
  "  set_target_properties(cpkt::lua PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_lua_facade_prefix}/lib/libcpkt_lua${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_lua_facade_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES Lua::Lua\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::lua_facade_shared)\n"
  "  add_library(cpkt::lua_facade_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::lua_facade_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_lua_facade_prefix}/lib/libcpkt_lua${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_lua_facade_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES cpkt::lua_shared\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("CpktLua" "CpktLua" "${CPKT_LUA_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/CpktLuaRuntime")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/CpktLuaRuntime/CpktLuaRuntimeConfig.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "get_filename_component(_cpkt_lua_runtime_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "find_dependency(Lua CONFIG REQUIRED)\n"
  "set(CpktLuaRuntime_FOUND TRUE)\n"
  "set(CpktLuaRuntime_VERSION \"${CPKT_LUA_VERSION}\")\n"
  "if(NOT TARGET cpkt::lua_runtime)\n"
  "  add_library(cpkt::lua_runtime STATIC IMPORTED)\n"
  "  set_target_properties(cpkt::lua_runtime PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_lua_runtime_prefix}/lib/libcpkt_lua_runtime${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_lua_runtime_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES Lua::Lua\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::lua_runtime_shared)\n"
  "  add_library(cpkt::lua_runtime_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::lua_runtime_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_lua_runtime_prefix}/lib/libcpkt_lua_runtime${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_lua_runtime_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES cpkt::lua_shared\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("CpktLuaRuntime" "CpktLuaRuntime" "${CPKT_LUA_VERSION}")

set(_cpkt_krb5_static_system_libraries "${CMAKE_DL_LIBS};Threads::Threads")
set(_cpkt_krb5_static_private_pc_libraries "-ldl -pthread")
if(CPKT_TARGET_ID MATCHES "-linux-")
  list(APPEND _cpkt_krb5_static_system_libraries resolv)
  string(APPEND _cpkt_krb5_static_private_pc_libraries " -lresolv")
elseif(CPKT_TARGET_ID MATCHES "-apple-darwin$")
  list(APPEND _cpkt_krb5_static_system_libraries resolv "-Wl,-framework,Kerberos")
  string(APPEND _cpkt_krb5_static_private_pc_libraries " -lresolv -Wl,-framework,Kerberos")
endif()
cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/Kerberos5")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/Kerberos5/Kerberos5Config.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "get_filename_component(_cpkt_krb5_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "find_dependency(Threads REQUIRED)\n"
  "find_dependency(OpenSSL CONFIG REQUIRED)\n"
  "set(Kerberos5_FOUND TRUE)\n"
  "set(Kerberos5_VERSION \"${CPKT_KRB5_VERSION}\")\n"
  "set(Kerberos5_INCLUDE_DIRS \"\${_cpkt_krb5_prefix}/include\")\n"
  "set(_cpkt_krb5_static_system_libraries \"${_cpkt_krb5_static_system_libraries}\")\n"
  "if(NOT TARGET cpkt::krb5_static)\n"
  "  add_library(cpkt::krb5_static STATIC IMPORTED)\n"
  "  set_target_properties(cpkt::krb5_static PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_krb5_prefix}/lib/libkrb5${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${Kerberos5_INCLUDE_DIRS}\"\n"
  "    INTERFACE_LINK_LIBRARIES \"\${_cpkt_krb5_prefix}/lib/libkrb5_k5tls${_cpkt_static_library_suffix};\${_cpkt_krb5_prefix}/lib/libk5crypto${_cpkt_static_library_suffix};\${_cpkt_krb5_prefix}/lib/libcom_err${_cpkt_static_library_suffix};\${_cpkt_krb5_prefix}/lib/libkrb5support${_cpkt_static_library_suffix};\${_cpkt_krb5_prefix}/lib/libprofile${_cpkt_static_library_suffix};\${_cpkt_krb5_prefix}/lib/libverto${_cpkt_static_library_suffix};OpenSSL::SSL;OpenSSL::Crypto;\${_cpkt_krb5_static_system_libraries}\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::gssapi_krb5_static)\n"
  "  add_library(cpkt::gssapi_krb5_static STATIC IMPORTED)\n"
  "  set_target_properties(cpkt::gssapi_krb5_static PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_krb5_prefix}/lib/libgssapi_krb5${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${Kerberos5_INCLUDE_DIRS}\"\n"
  "    INTERFACE_LINK_LIBRARIES cpkt::krb5_static\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::krb5_shared)\n"
  "  add_library(cpkt::krb5_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::krb5_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_krb5_prefix}/lib/libkrb5${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${Kerberos5_INCLUDE_DIRS}\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::gssapi_krb5_shared)\n"
  "  add_library(cpkt::gssapi_krb5_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::gssapi_krb5_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_krb5_prefix}/lib/libgssapi_krb5${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${Kerberos5_INCLUDE_DIRS}\"\n"
  "    INTERFACE_LINK_LIBRARIES cpkt::krb5_shared\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("Kerberos5" "Kerberos5" "${CPKT_KRB5_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/CyrusSASL")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/CyrusSASL/CyrusSASLConfig.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "find_dependency(Threads REQUIRED)\n"
  "get_filename_component(_cpkt_sasl_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "find_dependency(Kerberos5 CONFIG REQUIRED)\n"
  "find_dependency(OpenSSL CONFIG REQUIRED)\n"
  "set(CyrusSASL_FOUND TRUE)\n"
  "set(CyrusSASL_VERSION \"${CPKT_CYRUS_SASL_VERSION}\")\n"
  "if(NOT TARGET cpkt::cyrus_sasl_static)\n"
  "  add_library(cpkt::cyrus_sasl_static STATIC IMPORTED)\n"
  "  set_target_properties(cpkt::cyrus_sasl_static PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_sasl_prefix}/lib/libsasl2${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_sasl_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES \"cpkt::gssapi_krb5_static;OpenSSL::SSL;OpenSSL::Crypto;\${CMAKE_DL_LIBS};Threads::Threads\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::cyrus_sasl_shared)\n"
  "  add_library(cpkt::cyrus_sasl_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::cyrus_sasl_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_sasl_prefix}/lib/libsasl2${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_sasl_prefix}/include\"\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("CyrusSASL" "CyrusSASL" "${CPKT_CYRUS_SASL_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/CpktSasl")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/CpktSasl/CpktSaslConfig.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "get_filename_component(_cpkt_sasl_facade_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "find_dependency(CyrusSASL CONFIG REQUIRED)\n"
  "set(CpktSasl_FOUND TRUE)\n"
  "set(CpktSasl_VERSION \"${CPKT_CYRUS_SASL_VERSION}\")\n"
  "if(NOT TARGET cpkt::sasl)\n"
  "  add_library(cpkt::sasl STATIC IMPORTED)\n"
  "  set_target_properties(cpkt::sasl PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_sasl_facade_prefix}/lib/libcpkt_sasl${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_sasl_facade_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES cpkt::cyrus_sasl_static\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::sasl_shared)\n"
  "  add_library(cpkt::sasl_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::sasl_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_sasl_facade_prefix}/lib/libcpkt_sasl${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_sasl_facade_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES cpkt::cyrus_sasl_shared\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("CpktSasl" "CpktSasl" "${CPKT_CYRUS_SASL_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/OpenLDAP")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/OpenLDAP/OpenLDAPConfig.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "find_dependency(Threads REQUIRED)\n"
  "get_filename_component(_cpkt_ldap_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "find_dependency(CyrusSASL CONFIG REQUIRED)\n"
  "find_dependency(Kerberos5 CONFIG REQUIRED)\n"
  "find_dependency(OpenSSL CONFIG REQUIRED)\n"
  "set(OpenLDAP_FOUND TRUE)\n"
  "set(OpenLDAP_VERSION \"${CPKT_OPENLDAP_VERSION}\")\n"
  "if(NOT TARGET OpenLDAP::LDAP)\n"
  "  add_library(OpenLDAP::LDAP STATIC IMPORTED)\n"
  "  set_target_properties(OpenLDAP::LDAP PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_ldap_prefix}/lib/libldap${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_ldap_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES \"\${_cpkt_ldap_prefix}/lib/liblber${_cpkt_static_library_suffix};\${_cpkt_ldap_prefix}/lib/liblutil${_cpkt_static_library_suffix};cpkt::cyrus_sasl_static;OpenSSL::SSL;OpenSSL::Crypto;cpkt::gssapi_krb5_static;\${CMAKE_DL_LIBS};Threads::Threads\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::openldap_static)\n"
  "  add_library(cpkt::openldap_static INTERFACE IMPORTED)\n"
  "  set_target_properties(cpkt::openldap_static PROPERTIES\n"
  "    INTERFACE_LINK_LIBRARIES OpenLDAP::LDAP\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::openldap_shared)\n"
  "  add_library(cpkt::openldap_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::openldap_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_ldap_prefix}/lib/libldap${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_ldap_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES \"\${_cpkt_ldap_prefix}/lib/liblber${_cpkt_shared_library_suffix};cpkt::cyrus_sasl_shared;cpkt::openssl_ssl_shared;cpkt::openssl_crypto_shared;cpkt::gssapi_krb5_shared\"\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("OpenLDAP" "OpenLDAP" "${CPKT_OPENLDAP_VERSION}")

set(_cpkt_gssapi_static_system_libraries "")
set(_cpkt_gssapi_static_private_pc_libraries "-pthread")
if(CPKT_TARGET_ID MATCHES "-linux-")
  list(APPEND _cpkt_gssapi_static_system_libraries resolv)
  string(APPEND _cpkt_gssapi_static_private_pc_libraries " -ldl -lresolv")
elseif(CPKT_TARGET_ID MATCHES "-apple-darwin$")
  list(APPEND _cpkt_gssapi_static_system_libraries resolv "-Wl,-framework,Kerberos")
  string(APPEND _cpkt_gssapi_static_private_pc_libraries " -lresolv -Wl,-framework,Kerberos")
endif()
cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/CpktGssapi")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/CpktGssapi/CpktGssapiConfig.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "get_filename_component(_cpkt_gssapi_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "find_dependency(Threads REQUIRED)\n"
  "find_dependency(OpenSSL CONFIG REQUIRED)\n"
  "set(CpktGssapi_FOUND TRUE)\n"
  "set(CpktGssapi_VERSION \"${CPKT_KRB5_VERSION}\")\n"
  "if(NOT TARGET cpkt::gssapi)\n"
  "  add_library(cpkt::gssapi STATIC IMPORTED)\n"
  "  set_target_properties(cpkt::gssapi PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_gssapi_prefix}/lib/libcpkt_gssapi${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_gssapi_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES \"\${_cpkt_gssapi_prefix}/lib/libgssapi_krb5${_cpkt_static_library_suffix};\${_cpkt_gssapi_prefix}/lib/libkrb5${_cpkt_static_library_suffix};\${_cpkt_gssapi_prefix}/lib/libkrb5_k5tls${_cpkt_static_library_suffix};\${_cpkt_gssapi_prefix}/lib/libk5crypto${_cpkt_static_library_suffix};\${_cpkt_gssapi_prefix}/lib/libcom_err${_cpkt_static_library_suffix};\${_cpkt_gssapi_prefix}/lib/libkrb5support${_cpkt_static_library_suffix};\${_cpkt_gssapi_prefix}/lib/libprofile${_cpkt_static_library_suffix};\${_cpkt_gssapi_prefix}/lib/libverto${_cpkt_static_library_suffix};OpenSSL::SSL;OpenSSL::Crypto;\${CMAKE_DL_LIBS};Threads::Threads;${_cpkt_gssapi_static_system_libraries}\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::gssapi_shared)\n"
  "  add_library(cpkt::gssapi_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::gssapi_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_gssapi_prefix}/lib/libcpkt_gssapi${_cpkt_shared_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_gssapi_prefix}/include\"\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("CpktGssapi" "CpktGssapi" "${CPKT_KRB5_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/mqtt-c")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/mqtt-c/mqtt-cConfig.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "find_dependency(Threads REQUIRED)\n"
  "get_filename_component(_cpkt_mqttc_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "set(mqtt-c_FOUND TRUE)\n"
  "set(MQTTC_FOUND TRUE)\n"
  "set(MQTTC_INCLUDE_DIR \"\${_cpkt_mqttc_prefix}/include\")\n"
  "set(MQTTC_VERSION \"${CPKT_MQTTC_VERSION}\")\n"
  "set(MQTTC_COMMIT \"${CPKT_MQTTC_COMMIT}\")\n"
  "set(MQTTC_INCLUDE_DIRS \"\${MQTTC_INCLUDE_DIR}\")\n"
  "set(MQTTC_LIBRARIES MQTT-C::mqttc)\n"
  "if(NOT TARGET MQTT-C::mqttc)\n"
  "  add_library(MQTT-C::mqttc STATIC IMPORTED)\n"
  "  set_target_properties(MQTT-C::mqttc PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_mqttc_prefix}/lib/libmqttc${_cpkt_static_library_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${MQTTC_INCLUDE_DIR}\"\n"
  "    INTERFACE_LINK_LIBRARIES Threads::Threads\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::mqttc_shared)\n"
  "  add_library(cpkt::mqttc_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::mqttc_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_mqttc_prefix}/lib/${_cpkt_mqttc_shared_library_name}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${MQTTC_INCLUDE_DIR}\"\n"
  "    INTERFACE_LINK_LIBRARIES Threads::Threads\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("mqtt-c" "mqtt-c" "${CPKT_MQTTC_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/cmake/CpktMqttc")
cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/CpktMqttc/CpktMqttcConfig.cmake"
  "include(CMakeFindDependencyMacro)\n"
  "get_filename_component(_cpkt_mqttc_facade_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\n"
  "set(_cpkt_mqttc_static_suffix \".a\")\n"
  "if(APPLE)\n"
  "  set(_cpkt_mqttc_shared_suffix \".dylib\")\n"
  "else()\n"
  "  set(_cpkt_mqttc_shared_suffix \".so\")\n"
  "endif()\n"
  "find_dependency(mqtt-c CONFIG REQUIRED)\n"
  "set(CpktMqttc_FOUND TRUE)\n"
  "set(CpktMqttc_VERSION \"${CPKT_MQTTC_VERSION}\")\n"
  "if(NOT TARGET cpkt::mqttc)\n"
  "  add_library(cpkt::mqttc STATIC IMPORTED)\n"
  "  set_target_properties(cpkt::mqttc PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_mqttc_facade_prefix}/lib/libcpkt_mqttc\${_cpkt_mqttc_static_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_mqttc_facade_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES \"MQTT-C::mqttc\"\n"
  "  )\n"
  "endif()\n"
  "if(NOT TARGET cpkt::mqttc_facade_shared)\n"
  "  add_library(cpkt::mqttc_facade_shared SHARED IMPORTED)\n"
  "  set_target_properties(cpkt::mqttc_facade_shared PROPERTIES\n"
  "    IMPORTED_LOCATION \"\${_cpkt_mqttc_facade_prefix}/lib/libcpkt_mqttc\${_cpkt_mqttc_shared_suffix}\"\n"
  "    INTERFACE_INCLUDE_DIRECTORIES \"\${_cpkt_mqttc_facade_prefix}/include\"\n"
  "    INTERFACE_LINK_LIBRARIES cpkt::mqttc_shared\n"
  "  )\n"
  "endif()\n"
)
cpkt_write_config_version("CpktMqttc" "CpktMqttc" "${CPKT_MQTTC_VERSION}")

cpkt_metadata_file(MAKE_DIRECTORY "${_stage_root}/lib/pkgconfig")
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/libcrypto.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: OpenSSL-libcrypto\n"
  "Description: OpenSSL cryptography library from cpkt\n"
  "Version: ${CPKT_OPENSSL_VERSION}\n"
  "Libs: -L\${libdir} -lcrypto\n"
  "Libs.private: ${_cpkt_openssl_static_private_pc_libs}\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/libssl.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: OpenSSL-libssl\n"
  "Description: OpenSSL SSL/TLS library from cpkt\n"
  "Version: ${CPKT_OPENSSL_VERSION}\n"
  "Requires.private: libcrypto\n"
  "Libs: -L\${libdir} -lssl\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/openssl.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: OpenSSL\n"
  "Description: OpenSSL libraries from cpkt\n"
  "Version: ${CPKT_OPENSSL_VERSION}\n"
  "Requires.private: libssl libcrypto\n"
  "Libs: -L\${libdir} -lssl -lcrypto\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/cpkt-openssl.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: cpkt-openssl\n"
  "Description: C89 OpenSSL facade from cpkt\n"
  "Version: ${CPKT_OPENSSL_VERSION}\n"
  "Requires: openssl\n"
  "Libs: -L\${libdir} -lcpkt_openssl\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/cpkt-nghttp2.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: cpkt-nghttp2\n"
  "Description: C89 nghttp2 facade from cpkt\n"
  "Version: ${CPKT_NGHTTP2_VERSION}\n"
  "Requires.private: libnghttp2\n"
  "Libs: -L\${libdir} -lcpkt_nghttp2\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/cpkt-libssh2.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: cpkt-libssh2\n"
  "Description: C89 libssh2 facade from cpkt\n"
  "Version: ${CPKT_LIBSSH2_VERSION}\n"
  "Requires.private: libssh2\n"
  "Libs: -L\${libdir} -lcpkt_libssh2\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/zlib.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: zlib\n"
  "Description: zlib compression library from cpkt\n"
  "Version: ${CPKT_ZLIB_VERSION}\n"
  "Libs: -L\${libdir} -lz\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/libnghttp2.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: libnghttp2\n"
  "Description: HTTP/2 C library from cpkt\n"
  "Version: ${CPKT_NGHTTP2_VERSION}\n"
  "Libs: -L\${libdir} -lnghttp2\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/libssh2.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: libssh2\n"
  "Description: SSH2 client-side library from cpkt\n"
  "Version: ${CPKT_LIBSSH2_VERSION}\n"
  "Requires.private: libcrypto zlib\n"
  "Libs: -L\${libdir} -lssh2\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/libcurl.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: libcurl\n"
  "Description: URL transfer library from cpkt\n"
  "Version: ${CPKT_CURL_VERSION}\n"
  "Requires.private: libssh2 libnghttp2 libssl libcrypto zlib\n"
  "Libs: -L\${libdir} -lcurl\n"
  "Libs.private: -pthread ${_cpkt_curl_static_platform_pc_libs}\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/libxml-2.0.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: libXML\n"
  "Description: XML C parser and toolkit from cpkt\n"
  "Version: ${CPKT_LIBXML2_VERSION}\n"
  "Requires.private: zlib\n"
  "Libs: -L\${libdir} -lxml2\n"
  "Libs.private: ${_cpkt_libxml2_static_private_pc_libs_full}\n"
  "Cflags: -I\${includedir}/libxml2\n"
)
foreach(_lua_pc_name lua lua5.5)
  cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/${_lua_pc_name}.pc"
    "prefix=\${pcfiledir}/../..\n"
    "exec_prefix=\${prefix}\n"
    "libdir=\${prefix}/lib\n"
    "includedir=\${prefix}/include\n"
    "\n"
    "Name: Lua\n"
    "Description: Lua language runtime from cpkt\n"
    "Version: ${CPKT_LUA_VERSION}\n"
    "Libs: -L\${libdir} -llua\n"
    "Libs.private: ${_cpkt_lua_static_private_pc_libs}\n"
    "Cflags: -I\${includedir}\n"
  )
endforeach()
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/cpkt-lua.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: cpkt-lua\n"
  "Description: C89 Lua 5.5 facade from cpkt\n"
  "Version: ${CPKT_LUA_VERSION}\n"
  "Requires.private: lua\n"
  "Libs: -L\${libdir} -lcpkt_lua\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/cpkt-lua-runtime.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: cpkt-lua-runtime\n"
  "Description: C89-safe Lua runtime facade from cpkt\n"
  "Version: ${CPKT_LUA_VERSION}\n"
  "Requires.private: lua\n"
  "Libs: -L\${libdir} -lcpkt_lua_runtime\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/cpkt-gssapi.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: cpkt-gssapi\n"
  "Description: C89 GSSAPI facade from cpkt\n"
  "Version: ${CPKT_KRB5_VERSION}\n"
  "Requires.private: libssl libcrypto\n"
  "Libs: -L\${libdir} -lcpkt_gssapi\n"
  "Libs.private: -lgssapi_krb5 -lkrb5 -lkrb5_k5tls -lk5crypto -lcom_err -lkrb5support -lprofile -lverto ${_cpkt_gssapi_static_private_pc_libraries}\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/cpkt-sasl.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: cpkt-sasl\n"
  "Description: C89 Cyrus SASL facade from cpkt\n"
  "Version: ${CPKT_CYRUS_SASL_VERSION}\n"
  "Requires.private: libsasl2\n"
  "Libs: -L\${libdir} -lcpkt_sasl\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/krb5.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: MIT Kerberos\n"
  "Description: MIT Kerberos client libraries from cpkt\n"
  "Version: ${CPKT_KRB5_VERSION}\n"
  "Requires.private: libssl libcrypto\n"
  "Libs: -L\${libdir} -lkrb5 -lkrb5_k5tls -lk5crypto -lcom_err -lkrb5support -lprofile -lverto\n"
  "Libs.private: ${_cpkt_krb5_static_private_pc_libraries}\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/gssapi_krb5.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: MIT Kerberos GSSAPI\n"
  "Description: MIT Kerberos GSSAPI library from cpkt\n"
  "Version: ${CPKT_KRB5_VERSION}\n"
  "Requires.private: krb5\n"
  "Libs: -L\${libdir} -lgssapi_krb5\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/libsasl2.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: Cyrus SASL\n"
  "Description: Cyrus SASL client library from cpkt\n"
  "Version: ${CPKT_CYRUS_SASL_VERSION}\n"
  "Requires.private: gssapi_krb5 libssl libcrypto\n"
  "Libs: -L\${libdir} -lsasl2\n"
  "Libs.private: -ldl -pthread\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/ldap.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: OpenLDAP\n"
  "Description: OpenLDAP client library from cpkt\n"
  "Version: ${CPKT_OPENLDAP_VERSION}\n"
  "Requires.private: libsasl2 gssapi_krb5 libssl libcrypto\n"
  "Libs: -L\${libdir} -lldap -llber -llutil\n"
  "Libs.private: -ldl -pthread\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/mqtt-c.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: MQTT-C\n"
  "Description: Portable MQTT v3.1.1 client library from cpkt\n"
  "Version: ${CPKT_MQTTC_VERSION}\n"
  "Libs: -L\${libdir} -lmqttc\n"
  "Libs.private: -pthread\n"
  "Cflags: -I\${includedir}\n"
)
cpkt_metadata_file(WRITE "${_stage_root}/lib/pkgconfig/cpkt-mqttc.pc"
  "prefix=\${pcfiledir}/../..\n"
  "exec_prefix=\${prefix}\n"
  "libdir=\${prefix}/lib\n"
  "includedir=\${prefix}/include\n"
  "\n"
  "Name: cpkt-mqttc\n"
  "Description: C89 MQTT-C facade from cpkt\n"
  "Version: ${CPKT_MQTTC_VERSION}\n"
  "Requires.private: mqtt-c\n"
  "Libs: -L\${libdir} -lcpkt_mqttc\n"
  "Cflags: -I\${includedir}\n"
)
