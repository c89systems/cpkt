function(cpkt_add_openldap)
set(project_name "cpkt_openldap_project")
find_program(openldap_make_program NAMES gmake make REQUIRED)
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/openldap")
set(source_dir "${prefix_dir}/src")
set(build_dir "${source_dir}")
set(install_dir "${CPKT_EXTERNAL_ROOT}/openldap/install")
set(stage_dir "${install_dir}/stage")
set(stamp_dir "${prefix_dir}/stamp")
set(tmp_dir "${prefix_dir}/tmp")
set(ldap_static_library "${install_dir}/lib/libldap${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(lber_static_library "${install_dir}/lib/liblber${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(lutil_static_library "${install_dir}/lib/liblutil${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(ldap_shared_library "${install_dir}/lib/libldap${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(lber_shared_library "${install_dir}/lib/liblber${CMAKE_SHARED_LIBRARY_SUFFIX}")
cpkt_get_target_triple(target_triple)
cpkt_get_external_c_flags(external_cflags)
cpkt_get_autotools_link_flags(external_ldflags)
set(env_args "")
cpkt_append_pinned_external_toolchain_env_args(env_args)
list(APPEND env_args
    # OpenLDAP contributes static objects to cpkt::postgres.
    "CFLAGS=${external_cflags} -fPIC"
    "CPPFLAGS=-I${CPKT_CYRUS_SASL_PREFIX}/include -I${CPKT_OPENSSL_shared_PREFIX}/include -I${CPKT_KRB5_PREFIX}/include"
    "LDFLAGS=-L${CPKT_CYRUS_SASL_PREFIX}/lib -L${CPKT_OPENSSL_shared_PREFIX}/lib -L${CPKT_KRB5_PREFIX}/lib ${external_ldflags}")
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
set(openldap_rpath_rewrite_command ${CMAKE_COMMAND} -E true)
set(openldap_darwin_install_name_normalize_command ${CMAKE_COMMAND} -E true)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
set(openldap_rpath_rewrite_command
      ${CMAKE_COMMAND}
        -DCPKT_AUTOTOOLS_LIBTOOL=${build_dir}/libtool
        -P ${CMAKE_SOURCE_DIR}/cmake/disable_autotools_absolute_rpath.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(openldap_darwin_install_name_normalize_command
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
if(CPKT_BUILD_DEPENDENCIES)
cpkt_cached_external_project_add(${project_name}
      URL "https://www.openldap.org/software/download/OpenLDAP/openldap-release/openldap-${CPKT_OPENLDAP_VERSION}.tgz"
      URL_HASH "SHA256=bc91225dbfc50354033b1303bc91d1a7f6ddd1dc32fac950d79c28fe66d6bca8"
      DOWNLOAD_NAME "openldap-${CPKT_OPENLDAP_VERSION}.tgz"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      STAMP_DIR "${stamp_dir}"
      TMP_DIR "${tmp_dir}"
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      DEPENDS cpkt_cyrus_sasl_project cpkt_openssl_project cpkt_krb5_shared_project
      PATCH_COMMAND ${CMAKE_COMMAND}
        -DCPKT_OPENLDAP_SOURCE_DIR=${source_dir}
        -P ${CMAKE_SOURCE_DIR}/cmake/patch_openldap_lutil_link.cmake
        COMMAND ${CMAKE_COMMAND}
          -DCPKT_PATCH_WORKING_DIRECTORY=${source_dir}
          -DCPKT_PATCH_SERIES=${CMAKE_SOURCE_DIR}/cmake/patches/openldap.series
          -P ${CMAKE_SOURCE_DIR}/cmake/apply_patch_series.cmake
      CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args}
        "${source_dir}/configure"
        --host=${target_triple}
        --prefix=/usr
        --libdir=/usr/lib
        --includedir=/usr/include
        --sysconfdir=/etc/openldap
        --localstatedir=/var
        --enable-static
        --enable-shared
        --disable-fast-install
        --disable-slapd
        --disable-syslog
        --with-tls=openssl
        --with-cyrus-sasl
        --with-yielding_select=no
      BUILD_COMMAND ${openldap_rpath_rewrite_command}
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args}
        ${CMAKE_COMMAND}
          -DCPKT_OPENLDAP_BUILD_DIR=${build_dir}
          -DCPKT_OPENLDAP_MAKE_PROGRAM=${openldap_make_program}
          -DCPKT_OPENLDAP_BUILD_JOBS=${CPKT_DEPENDENCY_BUILD_JOBS}
          -P ${CMAKE_SOURCE_DIR}/cmake/build_openldap_libraries.cmake
      INSTALL_COMMAND ${CMAKE_COMMAND} -E remove_directory "${install_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${stage_dir}/usr/include" "${stage_dir}/usr/lib"
        COMMAND ${CMAKE_COMMAND} -E chdir "${build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C include install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND}
          -DCPKT_AUTOTOOLS_LIBRARY_SOURCE_DIR=${build_dir}/libraries/liblutil
          -DCPKT_AUTOTOOLS_LIBRARY_DESTINATION_DIR=${stage_dir}/usr/lib
          -DCPKT_AUTOTOOLS_LIBRARY_BASENAME=liblutil
          -P ${CMAKE_SOURCE_DIR}/cmake/copy_autotools_library_artifacts.cmake
        COMMAND ${CMAKE_COMMAND}
          -DCPKT_AUTOTOOLS_LIBRARY_SOURCE_DIR=${build_dir}/libraries/liblber/.libs
          -DCPKT_AUTOTOOLS_LIBRARY_DESTINATION_DIR=${stage_dir}/usr/lib
          -DCPKT_AUTOTOOLS_LIBRARY_BASENAME=liblber
          -P ${CMAKE_SOURCE_DIR}/cmake/copy_autotools_library_artifacts.cmake
        COMMAND ${CMAKE_COMMAND}
          -DCPKT_AUTOTOOLS_LIBRARY_SOURCE_DIR=${build_dir}/libraries/libldap/.libs
          -DCPKT_AUTOTOOLS_LIBRARY_DESTINATION_DIR=${stage_dir}/usr/lib
          -DCPKT_AUTOTOOLS_LIBRARY_BASENAME=libldap
          -P ${CMAKE_SOURCE_DIR}/cmake/copy_autotools_library_artifacts.cmake
        # OpenLDAP's libtool archive can embed liblutil.a as a nested member.
        # Darwin's linker rejects that archive; liblutil.a is staged and
        # exported separately in the supported static closure.
        COMMAND ${CMAKE_COMMAND}
          -DCPKT_STATIC_ARCHIVE=${stage_dir}/usr/lib/libldap${CMAKE_STATIC_LIBRARY_SUFFIX}
          -DCPKT_STATIC_ARCHIVE_MEMBER=liblutil${CMAKE_STATIC_LIBRARY_SUFFIX}
          -DCPKT_STATIC_ARCHIVER=${CMAKE_AR}
          -DCPKT_STATIC_RANLIB=${CMAKE_RANLIB}
          -P ${CMAKE_SOURCE_DIR}/cmake/remove_static_archive_member.cmake
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage_dir}/usr/include" "${install_dir}/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage_dir}/usr/lib" "${install_dir}/lib"
        COMMAND ${openldap_darwin_install_name_normalize_command}
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS "${ldap_static_library}" "${lber_static_library}" "${lutil_static_library}" "${ldap_shared_library}" "${lber_shared_library}"
      BUILD_IN_SOURCE 1
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()
add_library(cpkt::openldap_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::openldap_static PROPERTIES
    IMPORTED_LOCATION "${ldap_static_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
    INTERFACE_LINK_LIBRARIES "${lber_static_library};${lutil_static_library};cpkt::cyrus_sasl_static;cpkt::openssl_ssl_static;cpkt::openssl_crypto_static;cpkt::gssapi_krb5_static;${CMAKE_DL_LIBS};Threads::Threads")
add_library(cpkt::openldap_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::openldap_shared PROPERTIES
    IMPORTED_LOCATION "${ldap_shared_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
    INTERFACE_LINK_LIBRARIES "${lber_shared_library};cpkt::cyrus_sasl_shared;cpkt::openssl_ssl_shared;cpkt::openssl_crypto_shared;cpkt::gssapi_krb5_shared")
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::openldap_static ${project_name})
add_dependencies(cpkt::openldap_shared ${project_name})
cpkt_record_dependency_target(${project_name})
else()
cpkt_require_dependency_file("${ldap_static_library}" "OpenLDAP static library")
cpkt_require_dependency_file("${lber_static_library}" "OpenLDAP LBER static library")
cpkt_require_dependency_file("${ldap_shared_library}" "OpenLDAP shared library")
cpkt_require_dependency_file("${lber_shared_library}" "OpenLDAP LBER shared library")
cpkt_require_dependency_file("${install_dir}/include/ldap.h" "OpenLDAP header")
endif()
set(CPKT_OPENLDAP_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
