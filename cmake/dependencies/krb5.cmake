function(cpkt_add_krb5)
set(project_name_shared "cpkt_krb5_shared_project")
set(project_name_static "cpkt_krb5_static_project")
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/krb5")
set(source_dir "${prefix_dir}/src")
set(shared_build_dir "${prefix_dir}/build-shared")
set(static_build_dir "${prefix_dir}/build-static")
set(install_dir "${CPKT_EXTERNAL_ROOT}/krb5/install")
set(stage_dir "${install_dir}/stage")
set(stamp_dir "${prefix_dir}/stamp")
set(tmp_dir "${prefix_dir}/tmp")
set(gssapi_static_library "${install_dir}/lib/libgssapi_krb5${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(tls_static_library "${install_dir}/lib/libkrb5_k5tls${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(tls_module "${install_dir}/lib/krb5/plugins/tls/k5tls.so")
set(krb5_static_library "${install_dir}/lib/libkrb5${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(k5crypto_static_library "${install_dir}/lib/libk5crypto${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(com_err_static_library "${install_dir}/lib/libcom_err${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(krb5support_static_library "${install_dir}/lib/libkrb5support${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(profile_static_library "${install_dir}/lib/libprofile${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(verto_static_library "${install_dir}/lib/libverto${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(gssapi_shared_library "${install_dir}/lib/libgssapi_krb5${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(krb5_shared_library "${install_dir}/lib/libkrb5${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(com_err_header "${install_dir}/include/com_err.h")
set(krb5_static_platform_libraries "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
list(APPEND krb5_static_platform_libraries resolv)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
list(APPEND krb5_static_platform_libraries resolv "-Wl,-framework,Kerberos")
endif()
cpkt_get_target_triple(target_triple)
cpkt_get_external_c_flags(external_cflags)
string(APPEND external_cflags " -std=gnu17")
set(krb5_openssl_prefix "${CPKT_OPENSSL_shared_PREFIX}")
set(env_args "")
cpkt_append_pinned_external_toolchain_env_args(env_args)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
if(NOT EXISTS "${CPKT_DARWIN_HOST_MIG}" OR NOT EXISTS "${CPKT_DARWIN_HOST_MIGCOM}")
message(FATAL_ERROR "Darwin Kerberos requires the ready pinned host MIG toolchain")
endif()
get_filename_component(darwin_host_mig_bin_dir "${CPKT_DARWIN_HOST_MIG}" DIRECTORY)
if(CPKT_OSXCROSS_ROOT)
set(darwin_mig_path "${darwin_host_mig_bin_dir}:${CPKT_OSXCROSS_BIN_DIR}:$ENV{PATH}")
else()
set(darwin_mig_path "${darwin_host_mig_bin_dir}:$ENV{PATH}")
endif()
list(APPEND env_args
      "PATH=${darwin_mig_path}"
      "MIGCC=${CMAKE_C_COMPILER}"
      "MIGCOM=${CPKT_DARWIN_HOST_MIGCOM}"
      "SDKROOT=${CMAKE_OSX_SYSROOT}")
endif()
list(APPEND env_args
    # Kerberos static archives are part of the public GSSAPI closure.
    "CFLAGS=${external_cflags} -fPIC"
    "CPPFLAGS=-I${krb5_openssl_prefix}/include"
    # The static build has no runtime loader path.
    "LDFLAGS=-L${krb5_openssl_prefix}/lib"
    # Keep Kerberos defaults independent of the disposable build/install root.
    # Applications may override all three with the standard environment knobs.
    "DEFCCNAME=FILE:/tmp/krb5cc_%{uid}"
    "DEFKTNAME=FILE:/etc/krb5.keytab"
    "DEFCKTNAME=FILE:/var/lib/krb5/user/%{euid}/client.keytab")
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
list(APPEND env_args
      "WARN_CFLAGS=-Wno-error=discarded-qualifiers"
      "WARN_CXXFLAGS=-Wno-error=discarded-qualifiers")
endif()
if(CMAKE_CROSSCOMPILING)
list(APPEND env_args
      "krb5_cv_attr_constructor_destructor=yes,yes"
      "ac_cv_printf_positional=yes")
endif()
set(static_env_args ${env_args})
list(REMOVE_ITEM static_env_args "CFLAGS=${external_cflags} -fPIC")
list(APPEND static_env_args "CFLAGS=${external_cflags} -fPIC -DCPKT_KRB5_STATIC_TLS")
cpkt_get_autotools_link_flags(krb5_shared_link_flags)
list(REMOVE_ITEM env_args "LDFLAGS=-L${krb5_openssl_prefix}/lib")
list(APPEND env_args "LDFLAGS=-L${krb5_openssl_prefix}/lib ${krb5_shared_link_flags}")
set(krb5_plugin_link_flags "${krb5_shared_link_flags}")
string(REPLACE "ORIGIN" "ORIGIN/../../.." krb5_plugin_link_flags "${krb5_plugin_link_flags}")
string(REPLACE "@loader_path" "@loader_path/../../.." krb5_plugin_link_flags "${krb5_plugin_link_flags}")
set(krb5_plugin_ldflags "LDFLAGS=-L${krb5_openssl_prefix}/lib ${krb5_plugin_link_flags}")
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
set(krb5_darwin_install_name_normalize_command ${CMAKE_COMMAND} -E true)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(krb5_darwin_install_name_normalize_command
      ${CMAKE_COMMAND}
        -DCPKT_DARWIN_LIBRARY_DIR=${install_dir}/lib
        -DCPKT_DARWIN_STAGE_LIBRARY_DIR=${stage_dir}/usr/lib
        -DCPKT_DARWIN_INSTALL_NAME_TOOL=${CMAKE_INSTALL_NAME_TOOL}
        -DCPKT_DARWIN_OTOOL=${CPKT_OTOOL}
        -P ${CMAKE_SOURCE_DIR}/cmake/normalize_darwin_dylib_install_names.cmake)
endif()
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY
    "${install_dir}/include"
    "${install_dir}/include/gssapi"
    "${install_dir}/include/krb5"
    "${install_dir}/lib")
endif()
if(CPKT_BUILD_DEPENDENCIES)
cpkt_cached_external_project_add(${project_name_static}
      URL "https://web.mit.edu/kerberos/dist/krb5/1.22/krb5-${CPKT_KRB5_VERSION}.tar.gz"
      URL_HASH "SHA256=3243ffbc8ea4d4ac22ddc7dd2a1dc54c57874c40648b60ff97009763554eaf13"
      DOWNLOAD_NAME "krb5-${CPKT_KRB5_VERSION}.tar.gz"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${static_build_dir}"
      STAMP_DIR "${stamp_dir}/static"
      TMP_DIR "${tmp_dir}"
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      DEPENDS cpkt_openssl_project
      PATCH_COMMAND ${CMAKE_COMMAND}
        -DCPKT_PATCH_WORKING_DIRECTORY=${source_dir}
        -DCPKT_PATCH_SERIES=${CMAKE_SOURCE_DIR}/cmake/patches/krb5.series
        -P ${CMAKE_SOURCE_DIR}/cmake/apply_patch_series.cmake
      CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args}
        "${source_dir}/src/configure"
        --host=${target_triple}
        --prefix=/usr
        --libdir=/usr/lib
        --includedir=/usr/include
        --localstatedir=/var
        --disable-shared
        --enable-static
        --disable-rpath
        --disable-nls
        --disable-pkinit
        --with-tls-impl=openssl
        --without-ldap
        --without-readline
        COMMAND ${CMAKE_COMMAND}
          -DCPKT_KRB5_BUILD_DIR=${static_build_dir}
          -DCPKT_KRB5_REQUIRE_GLIBC_REENTRANT=${CPKT_TARGET_LIBC}
          -P ${CMAKE_SOURCE_DIR}/cmake/assert_krb5_features.cmake
      BUILD_COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C util/support -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C util/et -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C include -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C util -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C lib/crypto -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C lib/krb5 -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C lib/gssapi -j${CPKT_DEPENDENCY_BUILD_JOBS}
          "CFLAGS=${external_cflags} -fPIC -DCPKT_KRB5_STATIC_TLS -Werror=missing-prototypes"
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C plugins/tls/k5tls -j${CPKT_DEPENDENCY_BUILD_JOBS}
      INSTALL_COMMAND ${CMAKE_COMMAND} -E remove_directory "${install_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
          "${stage_dir}/usr/include"
          "${stage_dir}/usr/include/kadm5"
          "${stage_dir}/usr/include/krb5"
          "${stage_dir}/usr/include/gssapi"
          "${stage_dir}/usr/include/gssrpc"
          "${stage_dir}/usr/lib"
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C include install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C util/support install-libs DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C util/et install-libs DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C util/profile install-libs DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C util/verto install-libs DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C lib/crypto install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C lib/krb5 install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${static_build_dir}"
        ${CMAKE_COMMAND} -E env ${static_env_args} make -C lib/gssapi install DESTDIR=${stage_dir}
        # MIT Kerberos' include install omits the generated public com_err.h,
        # although its installed krb5.h includes it directly.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${static_build_dir}/include/com_err.h"
          "${stage_dir}/usr/include/com_err.h"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage_dir}/usr/include" "${install_dir}/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage_dir}/usr/lib" "${install_dir}/lib"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${static_build_dir}/plugins/tls/k5tls/libkrb5_k5tls${CMAKE_STATIC_LIBRARY_SUFFIX}"
          "${install_dir}/lib/libkrb5_k5tls${CMAKE_STATIC_LIBRARY_SUFFIX}"
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS
        "${gssapi_static_library}"
        "${krb5_static_library}"
        "${k5crypto_static_library}"
        "${com_err_static_library}"
        "${krb5support_static_library}"
        "${profile_static_library}"
        "${verto_static_library}"
        "${tls_static_library}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
cpkt_cached_external_project_add(${project_name_shared}
      URL "https://web.mit.edu/kerberos/dist/krb5/1.22/krb5-${CPKT_KRB5_VERSION}.tar.gz"
      URL_HASH "SHA256=3243ffbc8ea4d4ac22ddc7dd2a1dc54c57874c40648b60ff97009763554eaf13"
      DOWNLOAD_NAME "krb5-${CPKT_KRB5_VERSION}.tar.gz"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${shared_build_dir}"
      STAMP_DIR "${stamp_dir}/shared"
      TMP_DIR "${tmp_dir}"
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      DEPENDS ${project_name_static}
      PATCH_COMMAND ${CMAKE_COMMAND}
        -DCPKT_PATCH_WORKING_DIRECTORY=${source_dir}
        -DCPKT_PATCH_SERIES=${CMAKE_SOURCE_DIR}/cmake/patches/krb5.series
        -P ${CMAKE_SOURCE_DIR}/cmake/apply_patch_series.cmake
      CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args}
        "${source_dir}/src/configure"
        --host=${target_triple}
        --prefix=/usr
        --libdir=/usr/lib
        --includedir=/usr/include
        --localstatedir=/var
        --disable-static
        --enable-shared
        --disable-rpath
        --disable-nls
        --disable-pkinit
        --with-tls-impl=openssl
        --without-ldap
        --without-readline
        COMMAND ${CMAKE_COMMAND}
          -DCPKT_KRB5_BUILD_DIR=${shared_build_dir}
          -DCPKT_KRB5_REQUIRE_GLIBC_REENTRANT=${CPKT_TARGET_LIBC}
          -P ${CMAKE_SOURCE_DIR}/cmake/assert_krb5_features.cmake
      BUILD_COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C util/support -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C util/et -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C include -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C util -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C lib/crypto -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C lib/krb5 -j${CPKT_DEPENDENCY_BUILD_JOBS}
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C lib/gssapi -j${CPKT_DEPENDENCY_BUILD_JOBS}
          "CFLAGS=${external_cflags} -fPIC -Werror=missing-prototypes"
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C plugins/tls/k5tls -j${CPKT_DEPENDENCY_BUILD_JOBS}
          "${krb5_plugin_ldflags}"
      INSTALL_COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C include install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C util/support install-libs DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C util/et install-libs DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C util/profile install-libs DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C util/verto install-libs DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C lib/crypto install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C lib/krb5 install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C lib/gssapi install DESTDIR=${stage_dir}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${shared_build_dir}/include/com_err.h"
          "${stage_dir}/usr/include/com_err.h"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage_dir}/usr/include" "${install_dir}/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage_dir}/usr/lib" "${install_dir}/lib"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${stage_dir}/usr/lib/krb5/plugins/tls"
        COMMAND ${CMAKE_COMMAND} -E chdir "${shared_build_dir}"
        ${CMAKE_COMMAND} -E env ${env_args} make -C plugins/tls/k5tls install DESTDIR=${stage_dir}
          "${krb5_plugin_ldflags}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${stage_dir}/usr/lib/krb5/plugins/tls" "${install_dir}/lib/krb5/plugins/tls"
        COMMAND ${krb5_darwin_install_name_normalize_command}
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS "${gssapi_shared_library}" "${krb5_shared_library}" "${tls_module}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()
add_library(cpkt::gssapi_krb5_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::gssapi_krb5_static PROPERTIES
    IMPORTED_LOCATION "${gssapi_static_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
    INTERFACE_LINK_LIBRARIES "${krb5_static_library};${k5crypto_static_library};${com_err_static_library};${krb5support_static_library};${profile_static_library};${verto_static_library};${tls_static_library};cpkt::openssl_ssl_static;cpkt::openssl_crypto_static;${CMAKE_DL_LIBS};Threads::Threads;${krb5_static_platform_libraries}")
add_library(cpkt::gssapi_krb5_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::gssapi_krb5_shared PROPERTIES
    IMPORTED_LOCATION "${gssapi_shared_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include")
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::gssapi_krb5_static ${project_name_shared})
add_dependencies(cpkt::gssapi_krb5_shared ${project_name_shared})
cpkt_record_dependency_target(${project_name_shared})
else()
cpkt_require_dependency_file("${gssapi_static_library}" "MIT Kerberos GSSAPI static library")
cpkt_require_dependency_file("${gssapi_shared_library}" "MIT Kerberos GSSAPI shared library")
cpkt_require_dependency_file("${install_dir}/include/gssapi/gssapi.h" "MIT Kerberos GSSAPI header")
cpkt_require_dependency_file("${com_err_header}" "MIT Kerberos com_err header")
endif()
set(CPKT_KRB5_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
