function(cpkt_get_curl_platform_cmake_args out_var)
set(_args "")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
list(APPEND _args
      -DUSE_APPLE_SECTRUST=ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
list(APPEND _args
      -DCMAKE_SHARED_LINKER_FLAGS=-Wl,--enable-new-dtags
      -DCURL_CA_FALLBACK=ON)
endif()
set(${out_var} "${_args}" PARENT_SCOPE)
endfunction()

function(cpkt_get_curl_static_platform_libs out_var)
set(_libs "")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
list(APPEND _libs
      "-framework SystemConfiguration"
      "-framework Security"
      "-framework CoreFoundation"
      "-framework CoreServices")
endif()
set(${out_var} "${_libs}" PARENT_SCOPE)
endfunction()

function(cpkt_add_curl)
set(project_name "cpkt_curl_project")
set(openssl_project "cpkt_openssl_project")
set(nghttp2_project "cpkt_nghttp2_project")
set(libssh2_project "cpkt_libssh2_project")
set(zlib_project "cpkt_zlib_project")
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/curl")
set(install_dir "${CPKT_EXTERNAL_ROOT}/curl/install")
set(openssl_prefix "${CPKT_OPENSSL_shared_PREFIX}")
set(nghttp2_prefix "${CPKT_NGHTTP2_shared_PREFIX}")
set(libssh2_prefix "${CPKT_LIBSSH2_PREFIX}")
set(curl_download_name "curl-${CPKT_CURL_VERSION}.tar.xz")
set(curl_openssl_ssl_library "${openssl_prefix}/lib/libssl${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(curl_openssl_crypto_library "${openssl_prefix}/lib/libcrypto${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(curl_nghttp2_library "${nghttp2_prefix}/lib/libnghttp2${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(curl_libssh2_library "${libssh2_prefix}/lib/libssh2${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(curl_zlib_library "${CPKT_ZLIB_PREFIX}/lib/libz${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(source_dir "${prefix_dir}/src")
set(build_dir "${prefix_dir}/build")
set(stamp_dir "${prefix_dir}/stamp")
set(tmp_dir "${prefix_dir}/tmp")
cpkt_append_common_external_cmake_args(common_cmake_args)
cpkt_get_external_cmake_step_commands(cmake_build_command cmake_install_command "${install_dir}")
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include" "${install_dir}/lib")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(curl_install_rpath "@loader_path")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
set(curl_install_rpath "$ORIGIN")
else()
set(curl_install_rpath "")
endif()
cpkt_get_curl_platform_cmake_args(curl_platform_cmake_args)
cpkt_get_curl_static_platform_libs(curl_static_platform_libs)
cpkt_get_external_cmake_configure_command(cmake_configure_command)
set(curl_cmake_args
    -DCMAKE_INSTALL_PREFIX=${install_dir}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_BUILD_TYPE=${CPKT_DEPENDENCY_BUILD_TYPE}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_INSTALL_RPATH=${curl_install_rpath}
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF
    -DCMAKE_BUILD_RPATH=
    -DCMAKE_SKIP_INSTALL_RPATH=OFF
    ${curl_platform_cmake_args}
    -DBUILD_SHARED_LIBS=ON
    -DBUILD_STATIC_LIBS=ON
    -DSHARE_LIB_OBJECT=ON
    -DBUILD_CURL_EXE=OFF
    -DBUILD_EXAMPLES=OFF
    -DBUILD_LIBCURL_DOCS=OFF
    -DBUILD_MISC_DOCS=OFF
    -DBUILD_TESTING=OFF
    -DCURL_DISABLE_INSTALL=OFF
    -DCURL_USE_PKGCONFIG=OFF
    -DCURL_USE_OPENSSL=ON
    -DCURL_USE_LIBSSH2=ON
    -DCURL_USE_LIBSSH=OFF
    -DUSE_NGHTTP2=ON
    # libpq's OAuth support requires libcurl asynchronous DNS.  Keep this
    # resolver self-contained rather than introducing c-ares as another
    # bundled dependency.
    -DENABLE_THREADED_RESOLVER=ON
    -DCURL_DISABLE_LDAP=ON
    -DCURL_DISABLE_LDAPS=ON
    -DCURL_ZLIB=ON
    -DCURL_BROTLI=OFF
    -DCURL_ZSTD=OFF
    -DCURL_USE_LIBPSL=OFF
    -DUSE_LIBIDN2=OFF
    -DZLIB_ROOT=${CPKT_ZLIB_PREFIX}
    -DZLIB_INCLUDE_DIR=${CPKT_ZLIB_PREFIX}/include
    -DZLIB_LIBRARY=${curl_zlib_library}
    -DOPENSSL_ROOT_DIR=${openssl_prefix}
    -DOPENSSL_INCLUDE_DIR=${openssl_prefix}/include
    -DOPENSSL_SSL_LIBRARY=${curl_openssl_ssl_library}
    -DOPENSSL_CRYPTO_LIBRARY=${curl_openssl_crypto_library}
    -DNGHTTP2_INCLUDE_DIR=${nghttp2_prefix}/include
    -DNGHTTP2_LIBRARY=${curl_nghttp2_library}
    -DLIBSSH2_INCLUDE_DIR=${libssh2_prefix}/include
    -DLIBSSH2_LIBRARY=${curl_libssh2_library}
    ${common_cmake_args}
  )
if(CPKT_BUILD_DEPENDENCIES)
cpkt_cached_external_project_add(${project_name}
      URL "https://curl.se/download/curl-${CPKT_CURL_VERSION}.tar.xz"
      URL_HASH "SHA256=f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7"
      DOWNLOAD_NAME "${curl_download_name}"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${build_dir}"
      STAMP_DIR "${stamp_dir}"
      TMP_DIR "${tmp_dir}"
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      DEPENDS
        ${zlib_project}
        ${openssl_project}
        ${nghttp2_project}
        ${libssh2_project}
      CONFIGURE_COMMAND ${cmake_configure_command} ${curl_cmake_args}
      BUILD_COMMAND ${cmake_build_command}
      INSTALL_COMMAND ${cmake_install_command}
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS
        "${install_dir}/lib/libcurl${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "${install_dir}/lib/libcurl${CMAKE_SHARED_LIBRARY_SUFFIX}"
      BUILD_IN_SOURCE 0
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
endif()
add_library(cpkt::curl_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::curl_static
    PROPERTIES
      IMPORTED_LOCATION "${install_dir}/lib/libcurl${CMAKE_STATIC_LIBRARY_SUFFIX}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
      INTERFACE_LINK_LIBRARIES "cpkt::libssh2_static;cpkt::nghttp2_static;cpkt::openssl_ssl_static;cpkt::openssl_crypto_static;cpkt::zlib_static;${CMAKE_DL_LIBS};Threads::Threads;${curl_static_platform_libs}"
  )
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::curl_static ${project_name})
cpkt_record_dependency_target(${project_name})
else()
cpkt_require_dependency_file("${install_dir}/lib/libcurl${CMAKE_STATIC_LIBRARY_SUFFIX}" "curl (static)")
endif()
add_library(cpkt::curl_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::curl_shared
    PROPERTIES
      IMPORTED_LOCATION "${install_dir}/lib/libcurl${CMAKE_SHARED_LIBRARY_SUFFIX}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
      INTERFACE_LINK_LIBRARIES "cpkt::openssl_ssl_shared;cpkt::openssl_crypto_shared;cpkt::nghttp2_shared;cpkt::libssh2_shared;cpkt::zlib_shared;${CMAKE_DL_LIBS};Threads::Threads"
  )
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::curl_shared ${project_name})
else()
cpkt_require_dependency_file("${install_dir}/lib/libcurl${CMAKE_SHARED_LIBRARY_SUFFIX}" "curl (shared)")
endif()
set(CPKT_CURL_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
