function(cpkt_get_openssl_config_args out_var)
cpkt_get_openssl_config_target(openssl_config_target)
set(_args ${openssl_config_target} no-tests no-docs no-module no-apps no-makedepend)
if(CPKT_TARGET_LIBC STREQUAL "musl")
list(APPEND _args no-secure-memory no-afalgeng)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
list(APPEND _args shared "-Wl,--enable-new-dtags,-rpath,\\\\$$ORIGIN")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
list(APPEND _args shared "-Wl,-rpath,@loader_path")
else()
list(APPEND _args shared)
endif()
set(${out_var} "${_args}" PARENT_SCOPE)
endfunction()

function(cpkt_add_openssl)
set(project_name "cpkt_openssl_project")
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/openssl")
set(source_dir "${prefix_dir}/src")
set(build_dir "${prefix_dir}/build")
set(install_dir "${CPKT_EXTERNAL_ROOT}/openssl/install")
set(stamp_dir "${prefix_dir}/stamp")
set(tmp_dir "${prefix_dir}/tmp")
set(openssl_dir "/etc/ssl")
cpkt_get_openssl_config_args(config_args)
cpkt_normalize_prefix(env_prefix "${install_dir}")
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include" "${install_dir}/lib")
endif()
set(build_command make -j${CPKT_DEPENDENCY_BUILD_JOBS})
set(install_command make -j${CPKT_DEPENDENCY_BUILD_JOBS} install_sw DESTDIR=${env_prefix})
set(openssl_post_configure_command "")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(openssl_post_configure_command
      COMMAND ${CMAKE_COMMAND}
        -DCPKT_DARWIN_INSTALL_NAME_FILE=${source_dir}/Makefile
        -P ${CMAKE_SOURCE_DIR}/cmake/patch_darwin_generated_install_names.cmake)
endif()
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
set(openssl_env_args
    CC=${CMAKE_C_COMPILER}
    AR=${CMAKE_AR}
    RANLIB=${CMAKE_RANLIB}
  )
cpkt_get_external_c_flags(openssl_cflags)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
string(REPLACE " -include stdint.h -include sys/types.h" "" openssl_cflags "${openssl_cflags}")
endif()
list(APPEND openssl_env_args CFLAGS=${openssl_cflags})
cpkt_append_pinned_external_toolchain_env_args(openssl_env_args)
if(CPKT_BUILD_DEPENDENCIES)
cpkt_cached_external_project_add(${project_name}
      URL "https://github.com/openssl/openssl/releases/download/openssl-${CPKT_OPENSSL_VERSION}/openssl-${CPKT_OPENSSL_VERSION}.tar.gz"
      URL_HASH "SHA256=9bffaa1ad1e07b354c21bd3324ec02fa15579f45a7d0494b3e74bc449b7333ef"
      DOWNLOAD_NAME "openssl-${CPKT_OPENSSL_VERSION}.tar.gz"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      STAMP_DIR "${stamp_dir}"
      TMP_DIR "${tmp_dir}"
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      PATCH_COMMAND
        ${CMAKE_COMMAND}
          -DOPENSSL_SOURCE_DIR=${source_dir}
          -P ${CMAKE_SOURCE_DIR}/cmake/patch_openssl_buildinfo.cmake
      CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env
          ${openssl_env_args}
          "${source_dir}/Configure"
          ${config_args}
          --prefix=/
          --openssldir=${openssl_dir}
          --libdir=lib
        ${openssl_post_configure_command}
      BUILD_COMMAND ${CMAKE_COMMAND} -E env ${openssl_env_args} ${build_command}
      INSTALL_COMMAND ${CMAKE_COMMAND} -E env ${openssl_env_args} ${install_command}
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS
        "${install_dir}/lib/libcrypto${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "${install_dir}/lib/libssl${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "${install_dir}/lib/libcrypto${CMAKE_SHARED_LIBRARY_SUFFIX}"
        "${install_dir}/lib/libssl${CMAKE_SHARED_LIBRARY_SUFFIX}"
      BUILD_IN_SOURCE 1
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
endif()
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include" "${install_dir}/lib")
endif()
set(openssl_crypto_static_extra_libs "")
if(CPKT_TARGET_ARCH STREQUAL "armhf")
list(APPEND openssl_crypto_static_extra_libs atomic)
endif()
add_library(cpkt::openssl_crypto_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::openssl_crypto_static
    PROPERTIES
      IMPORTED_LOCATION "${install_dir}/lib/libcrypto${CMAKE_STATIC_LIBRARY_SUFFIX}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
      INTERFACE_LINK_LIBRARIES "${openssl_crypto_static_extra_libs}"
  )
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::openssl_crypto_static ${project_name})
cpkt_record_dependency_target(${project_name})
else()
cpkt_require_dependency_file("${install_dir}/lib/libcrypto${CMAKE_STATIC_LIBRARY_SUFFIX}" "OpenSSL crypto (static)")
endif()
add_library(cpkt::openssl_ssl_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::openssl_ssl_static
    PROPERTIES
      IMPORTED_LOCATION "${install_dir}/lib/libssl${CMAKE_STATIC_LIBRARY_SUFFIX}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
      INTERFACE_LINK_LIBRARIES "cpkt::openssl_crypto_static;${CMAKE_DL_LIBS};Threads::Threads"
  )
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::openssl_ssl_static ${project_name})
else()
cpkt_require_dependency_file("${install_dir}/lib/libssl${CMAKE_STATIC_LIBRARY_SUFFIX}" "OpenSSL ssl (static)")
endif()
add_library(cpkt::openssl_crypto_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::openssl_crypto_shared
    PROPERTIES
      IMPORTED_LOCATION "${install_dir}/lib/libcrypto${CMAKE_SHARED_LIBRARY_SUFFIX}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
  )
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::openssl_crypto_shared ${project_name})
else()
cpkt_require_dependency_file("${install_dir}/lib/libcrypto${CMAKE_SHARED_LIBRARY_SUFFIX}" "OpenSSL crypto (shared)")
endif()
add_library(cpkt::openssl_ssl_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::openssl_ssl_shared
    PROPERTIES
      IMPORTED_LOCATION "${install_dir}/lib/libssl${CMAKE_SHARED_LIBRARY_SUFFIX}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
      INTERFACE_LINK_LIBRARIES "cpkt::openssl_crypto_shared;${CMAKE_DL_LIBS};Threads::Threads"
  )
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::openssl_ssl_shared ${project_name})
else()
cpkt_require_dependency_file("${install_dir}/lib/libssl${CMAKE_SHARED_LIBRARY_SUFFIX}" "OpenSSL ssl (shared)")
endif()
set(CPKT_OPENSSL_static_PREFIX "${install_dir}" PARENT_SCOPE)
set(CPKT_OPENSSL_shared_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
