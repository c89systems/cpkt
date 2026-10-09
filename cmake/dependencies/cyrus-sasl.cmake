function(cpkt_add_cyrus_sasl)
set(project_name "cpkt_cyrus_sasl_project")
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/cyrus-sasl")
set(source_dir "${prefix_dir}/src")
set(build_dir "${prefix_dir}/build")
set(install_dir "${CPKT_EXTERNAL_ROOT}/cyrus-sasl/install")
set(stage_dir "${install_dir}/stage")
set(stamp_dir "${prefix_dir}/stamp")
set(tmp_dir "${prefix_dir}/tmp")
set(static_library "${install_dir}/lib/libsasl2${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(shared_library "${install_dir}/lib/libsasl2${CMAKE_SHARED_LIBRARY_SUFFIX}")
cpkt_get_target_triple(target_triple)
cpkt_get_external_c_flags(external_cflags)
cpkt_get_autotools_link_flags(external_ldflags)
set(cyrus_sasl_cppflags "-DPROTOTYPES=1 -DHAVE_TIME_H=1")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
string(APPEND cyrus_sasl_cppflags " -D_GNU_SOURCE")
string(APPEND external_ldflags
      " -Wl,-rpath,\\\\$$ORIGIN/.. -Wl,-rpath-link,${CPKT_KRB5_PREFIX}/lib")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
string(APPEND external_ldflags " -Wl,-rpath,@loader_path/..")
endif()
set(env_args "")
cpkt_append_pinned_external_toolchain_env_args(env_args)
list(APPEND env_args
    # Cyrus SASL contributes static objects to cpkt::postgres.
    "CFLAGS=${external_cflags} -fPIC"
    # Cyrus SASL 2.1.28 defaults its bundled MD5 code to K&R declarations
    # unless the build tells it that the compiler has ANSI prototypes.
    "CPPFLAGS=${cyrus_sasl_cppflags} -I${CPKT_KRB5_PREFIX}/include -I${CPKT_OPENSSL_shared_PREFIX}/include"
    "LDFLAGS=-L${CPKT_KRB5_PREFIX}/lib -L${CPKT_OPENSSL_shared_PREFIX}/lib ${external_ldflags}"
    # Cyrus's CMU_HAVE_OPENSSL macro normally adds an absolute rpath for its
    # OpenSSL prefix.  This cache value keeps the link search path while
    # leaving the installed library relocatable.
    "andrew_cv_runpath_switch=none")
if(CMAKE_CROSSCOMPILING)
list(APPEND env_args "ac_cv_gssapi_supports_spnego=yes")
endif()
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
set(cyrus_sasl_platform_configure_args "")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
list(APPEND cyrus_sasl_platform_configure_args --disable-macos-framework)
endif()
set(cyrus_sasl_rpath_rewrite_command ${CMAKE_COMMAND} -E true)
set(cyrus_sasl_darwin_install_name_normalize_command ${CMAKE_COMMAND} -E true)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
set(cyrus_sasl_rpath_rewrite_command
      ${CMAKE_COMMAND}
        -DCPKT_AUTOTOOLS_LIBTOOL=${build_dir}/libtool
        -P ${CMAKE_SOURCE_DIR}/cmake/disable_autotools_absolute_rpath.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(cyrus_sasl_darwin_install_name_normalize_command
      ${CMAKE_COMMAND}
        -DCPKT_DARWIN_LIBRARY_DIR=${install_dir}/lib
        -DCPKT_DARWIN_STAGE_LIBRARY_DIR=${stage_dir}/usr/lib
        -DCPKT_DARWIN_INSTALL_NAME_TOOL=${CMAKE_INSTALL_NAME_TOOL}
        -DCPKT_DARWIN_OTOOL=${CPKT_OTOOL}
        -P ${CMAKE_SOURCE_DIR}/cmake/normalize_darwin_dylib_install_names.cmake)
endif()
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include" "${install_dir}/lib")
endif()
set(cyrus_sasl_md5_header_dir "${prefix_dir}/generated")
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
set(CPKT_CYRUS_SASL_MD5_INT8_TYPE "long long")
set(CPKT_CYRUS_SASL_MD5_UINT8_TYPE "unsigned long long")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
set(CPKT_CYRUS_SASL_MD5_INT8_TYPE "long")
set(CPKT_CYRUS_SASL_MD5_UINT8_TYPE "unsigned long")
else()
message(FATAL_ERROR "Cyrus SASL has no md5global.h recipe for ${CMAKE_SIZEOF_VOID_P}-byte pointers")
endif()
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${cyrus_sasl_md5_header_dir}")
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/cyrus_sasl_md5global.h.in"
    "${cyrus_sasl_md5_header_dir}/md5global.h"
    @ONLY)
endif()
if(CPKT_BUILD_DEPENDENCIES)
cpkt_cached_external_project_add(${project_name}
      URL "https://github.com/cyrusimap/cyrus-sasl/releases/download/cyrus-sasl-${CPKT_CYRUS_SASL_VERSION}/cyrus-sasl-${CPKT_CYRUS_SASL_VERSION}.tar.gz"
      URL_HASH "SHA256=7ccfc6abd01ed67c1a0924b353e526f1b766b21f42d4562ee635a8ebfc5bb38c"
      DOWNLOAD_NAME "cyrus-sasl-${CPKT_CYRUS_SASL_VERSION}.tar.gz"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${build_dir}"
      STAMP_DIR "${stamp_dir}"
      TMP_DIR "${tmp_dir}"
      PATCH_COMMAND ${CMAKE_COMMAND}
        -DCPKT_PATCH_WORKING_DIRECTORY=${source_dir}
        -DCPKT_PATCH_SERIES=${CMAKE_SOURCE_DIR}/cmake/patches/cyrus_sasl.series
        -P ${CMAKE_SOURCE_DIR}/cmake/apply_patch_series.cmake
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      DEPENDS cpkt_krb5_shared_project cpkt_openssl_project
      CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args}
        "${source_dir}/configure"
        --host=${target_triple}
        --prefix=/usr
        --libdir=/usr/lib
        --with-lib-subdir=lib
        --includedir=/usr/include
        --sysconfdir=/etc
        --enable-static
        --enable-shared
        --disable-sample
        --disable-obsolete_cram_attr
        --disable-obsolete_digest_attr
        --disable-checkapop
        --disable-cram
        --disable-digest
        --disable-scram
        --disable-otp
        --disable-plain
        --disable-anon
        --without-saslauthd
        --enable-gssapi=${CPKT_KRB5_PREFIX}
        --with-gss_impl=mit
        --with-openssl=${CPKT_OPENSSL_shared_PREFIX}
        ${cyrus_sasl_platform_configure_args}
        COMMAND ${CMAKE_COMMAND}
          -DCPKT_CYRUS_SASL_BUILD_DIR=${build_dir}
          -P ${CMAKE_SOURCE_DIR}/cmake/assert_cyrus_sasl_gssapi.cmake
        COMMAND ${CMAKE_COMMAND}
          -DCPKT_CYRUS_SASL_BUILD_DIR=${build_dir}
          -P ${CMAKE_SOURCE_DIR}/cmake/enable_cyrus_sasl_static_gs2.cmake
      BUILD_COMMAND ${cyrus_sasl_rpath_rewrite_command}
        # Cyrus SASL 2.1.28's out-of-tree Makefile suppresses the makemd5
        # host tool when both build and target executable suffixes are empty,
        # but still unconditionally requires its generated header.  It also
        # considers that missing prerequisite newer than a pre-copied header.
        # A timestamp-only placeholder followed by the CMake-generated header
        # keeps the prerequisite older without building or executing a host
        # helper.
        COMMAND ${CMAKE_COMMAND} -E touch "${build_dir}/include/makemd5"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${cyrus_sasl_md5_header_dir}/md5global.h" "${build_dir}/include/md5global.h"
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C include -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C common -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C lib -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C sasldb -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C plugins -j${CPKT_DEPENDENCY_BUILD_JOBS}
      INSTALL_COMMAND ${CMAKE_COMMAND} -E remove_directory "${install_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${stage_dir}/usr/include" "${stage_dir}/usr/lib"
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C include install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C lib install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C plugins install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage_dir}/usr/include" "${install_dir}/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage_dir}/usr/lib" "${install_dir}/lib"
        COMMAND ${CMAKE_COMMAND}
          -DCPKT_CYRUS_SASL_INSTALL_DIR=${install_dir}
          -DCPKT_CYRUS_SASL_MODULE_SUFFIX=${CMAKE_SHARED_MODULE_SUFFIX}
          -P ${CMAKE_SOURCE_DIR}/cmake/assert_cyrus_sasl_plugins.cmake
        COMMAND ${cyrus_sasl_darwin_install_name_normalize_command}
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS "${static_library}" "${shared_library}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()
add_library(cpkt::cyrus_sasl_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::cyrus_sasl_static PROPERTIES
    IMPORTED_LOCATION "${static_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
    INTERFACE_LINK_LIBRARIES "cpkt::gssapi_krb5_static;cpkt::openssl_ssl_static;cpkt::openssl_crypto_static;${CMAKE_DL_LIBS};Threads::Threads")
add_library(cpkt::cyrus_sasl_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::cyrus_sasl_shared PROPERTIES
    IMPORTED_LOCATION "${shared_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include")
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::cyrus_sasl_static ${project_name})
add_dependencies(cpkt::cyrus_sasl_shared ${project_name})
cpkt_record_dependency_target(${project_name})
else()
cpkt_require_dependency_file("${static_library}" "Cyrus SASL static library")
cpkt_require_dependency_file("${shared_library}" "Cyrus SASL shared library")
cpkt_require_dependency_file("${install_dir}/include/sasl/sasl.h" "Cyrus SASL header")
endif()
set(CPKT_CYRUS_SASL_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
