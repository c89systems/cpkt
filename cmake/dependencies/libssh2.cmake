function(cpkt_add_libssh2)
set(project_name "cpkt_libssh2_project")
set(openssl_project "")
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/libssh2")
set(source_dir "${prefix_dir}/src")
set(build_dir "${prefix_dir}/build")
set(install_dir "${CPKT_EXTERNAL_ROOT}/libssh2/install")
set(stamp_dir "${prefix_dir}/stamp")
set(tmp_dir "${prefix_dir}/tmp")
cpkt_append_common_external_cmake_args(common_cmake_args)
cpkt_get_external_cmake_step_commands(cmake_build_command cmake_install_command "${install_dir}")
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include" "${install_dir}/lib")
endif()
set(libssh2_shared_library "${install_dir}/lib/libssh2${CMAKE_SHARED_LIBRARY_SUFFIX}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(libssh2_shared_library "${install_dir}/lib/libssh2.1${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
set(libssh2_static_library "${install_dir}/lib/libssh2${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(NOT DEFINED CPKT_ZLIB_PREFIX OR "${CPKT_ZLIB_PREFIX}" STREQUAL "")
message(FATAL_ERROR "libssh2 requires zlib to be configured first")
endif()
if(DEFINED CPKT_OPENSSL_shared_PREFIX AND NOT "${CPKT_OPENSSL_shared_PREFIX}" STREQUAL "")
set(libssh2_openssl_prefix "${CPKT_OPENSSL_shared_PREFIX}")
set(libssh2_openssl_build_variant "shared")
set(openssl_project "cpkt_openssl_project")
set(libssh2_openssl_ssl_library "${libssh2_openssl_prefix}/lib/libssl${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(libssh2_openssl_crypto_library "${libssh2_openssl_prefix}/lib/libcrypto${CMAKE_SHARED_LIBRARY_SUFFIX}")
elseif(DEFINED CPKT_OPENSSL_static_PREFIX AND NOT "${CPKT_OPENSSL_static_PREFIX}" STREQUAL "")
set(libssh2_openssl_prefix "${CPKT_OPENSSL_static_PREFIX}")
set(libssh2_openssl_build_variant "static")
set(openssl_project "cpkt_openssl_project")
set(libssh2_openssl_ssl_library "${libssh2_openssl_prefix}/lib/libssl${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(libssh2_openssl_crypto_library "${libssh2_openssl_prefix}/lib/libcrypto${CMAKE_STATIC_LIBRARY_SUFFIX}")
else()
message(FATAL_ERROR "libssh2 requires OpenSSL to be configured first")
endif()
if(DEFINED CPKT_OPENSSL_static_PREFIX AND NOT "${CPKT_OPENSSL_static_PREFIX}" STREQUAL "")
set(libssh2_openssl_link_variant "static")
else()
set(libssh2_openssl_link_variant "${libssh2_openssl_build_variant}")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(libssh2_install_rpath "@loader_path")
set(libssh2_platform_cmake_args "")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
set(libssh2_install_rpath "$ORIGIN")
set(libssh2_platform_cmake_args
      -DCMAKE_SHARED_LINKER_FLAGS=-Wl,--enable-new-dtags)
else()
set(libssh2_install_rpath "")
set(libssh2_platform_cmake_args "")
endif()
if(CPKT_BUILD_DEPENDENCIES)
cpkt_external_install_byproducts(libssh2_install_byproducts
      "${install_dir}/include/libssh2.h"
      "${install_dir}/include/libssh2_sftp.h"
      "${install_dir}/include/libssh2_publickey.h")
cpkt_cached_external_project_add(${project_name}
      URL "https://libssh2.org/download/libssh2-${CPKT_LIBSSH2_VERSION}.tar.gz"
      URL_HASH "SHA256=d9ec76cbe34db98eec3539fe2c899d26b0c837cb3eb466a56b0f109cabf658f7"
      DOWNLOAD_NAME "libssh2-${CPKT_LIBSSH2_VERSION}.tar.gz"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${build_dir}"
      STAMP_DIR "${stamp_dir}"
      TMP_DIR "${tmp_dir}"
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      PATCH_COMMAND ${CMAKE_COMMAND}
        -DCPKT_LIBSSH2_SOURCE_DIR=<SOURCE_DIR>
        -P ${CMAKE_SOURCE_DIR}/cmake/patch_libssh2_single_pass.cmake
        COMMAND ${CMAKE_COMMAND}
          -DCPKT_LIBSSH2_SOURCE_DIR=<SOURCE_DIR>
          -P ${CMAKE_SOURCE_DIR}/cmake/patch_libssh2_poll_elapsed.cmake
      CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${install_dir}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=${CPKT_DEPENDENCY_BUILD_TYPE}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON
        -DCMAKE_INSTALL_RPATH=${libssh2_install_rpath}
        -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF
        -DCMAKE_BUILD_RPATH=
        -DCMAKE_SKIP_INSTALL_RPATH=OFF
        ${libssh2_platform_cmake_args}
        -DBUILD_STATIC_LIBS=ON
        -DBUILD_SHARED_LIBS=ON
        -DBUILD_EXAMPLES=OFF
        -DBUILD_TESTING=OFF
        -DENABLE_ZLIB_COMPRESSION=ON
        -DCRYPTO_BACKEND=OpenSSL
        -DOpenSSL_DIR=${libssh2_openssl_prefix}/lib/cmake/OpenSSL
        -DZLIB_ROOT=${CPKT_ZLIB_PREFIX}
        -DZLIB_DIR=${CPKT_ZLIB_PREFIX}/lib/cmake/zlib
        -DZLIB_INCLUDE_DIRS=${CPKT_ZLIB_PREFIX}/include
        -DZLIB_LIBRARIES=${CPKT_ZLIB_SHARED_LIBRARY}
        ${common_cmake_args}
      DEPENDS
        ${openssl_project}
        cpkt_zlib_project
      BUILD_COMMAND ${cmake_build_command}
      INSTALL_COMMAND ${cmake_install_command}
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS
        "${libssh2_static_library}"
        "${libssh2_shared_library}"
      ${libssh2_install_byproducts}
      BUILD_IN_SOURCE 0
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
endif()
add_library(cpkt::libssh2_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::libssh2_static
    PROPERTIES
      IMPORTED_LOCATION "${libssh2_static_library}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
      INTERFACE_LINK_LIBRARIES "cpkt::openssl_crypto_${libssh2_openssl_link_variant};cpkt::zlib_static"
  )
add_library(cpkt::libssh2_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::libssh2_shared
    PROPERTIES
      IMPORTED_LOCATION "${libssh2_shared_library}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
      INTERFACE_LINK_LIBRARIES "cpkt::zlib_shared"
  )
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::libssh2_static ${project_name})
add_dependencies(cpkt::libssh2_shared ${project_name})
cpkt_record_dependency_target(${project_name})
else()
cpkt_require_dependency_file("${libssh2_static_library}" "libssh2 static library")
cpkt_require_dependency_file("${libssh2_shared_library}" "libssh2 shared library")
cpkt_require_dependency_file("${install_dir}/include/libssh2.h" "libssh2 header")
cpkt_require_dependency_file("${install_dir}/include/libssh2_publickey.h" "libssh2 publickey header")
cpkt_require_dependency_file("${install_dir}/include/libssh2_sftp.h" "libssh2 sftp header")
endif()
set(CPKT_LIBSSH2_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
