function(cpkt_add_nghttp2)
set(project_name "cpkt_nghttp2_project")
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/nghttp2")
set(source_dir "${prefix_dir}/src")
set(build_dir "${prefix_dir}/build")
set(install_dir "${CPKT_EXTERNAL_ROOT}/nghttp2/install")
set(stamp_dir "${prefix_dir}/stamp")
set(tmp_dir "${prefix_dir}/tmp")
cpkt_get_target_triple(autotools_host)
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include" "${install_dir}/lib")
endif()
set(nghttp2_env_args
    CC=${CMAKE_C_COMPILER}
    AR=${CMAKE_AR}
    RANLIB=${CMAKE_RANLIB}
  )
set(nghttp2_post_configure_command "")
cpkt_get_external_c_flags(nghttp2_cflags)
list(APPEND nghttp2_env_args CFLAGS=${nghttp2_cflags})
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_LINKER)
set(nghttp2_post_configure_command
      COMMAND ${CMAKE_COMMAND}
        -DCPKT_DARWIN_INSTALL_NAME_FILE=${build_dir}/libtool
        -P ${CMAKE_SOURCE_DIR}/cmake/patch_darwin_generated_install_names.cmake)
endif()
cpkt_append_pinned_external_toolchain_env_args(nghttp2_env_args)
if(CPKT_BUILD_DEPENDENCIES)
cpkt_external_install_byproducts(nghttp2_install_byproducts
      "${install_dir}/include/nghttp2/nghttp2.h"
      "${install_dir}/include/nghttp2/nghttp2ver.h")
cpkt_cached_external_project_add(${project_name}
      URL "https://github.com/nghttp2/nghttp2/releases/download/v${CPKT_NGHTTP2_VERSION}/nghttp2-${CPKT_NGHTTP2_VERSION}.tar.gz"
      URL_HASH "SHA256=aa317e2cf9dca6afa0aed68f8fad6ff303ec6982e25a78c75c0b65e2b9b3ded5"
      DOWNLOAD_NAME "nghttp2-${CPKT_NGHTTP2_VERSION}.tar.gz"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${build_dir}"
      STAMP_DIR "${stamp_dir}"
      TMP_DIR "${tmp_dir}"
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env
        ${nghttp2_env_args}
        "${source_dir}/configure"
        --prefix=${install_dir}
        --host=${autotools_host}
        --enable-shared
        --enable-static
        --with-pic
        --enable-lib-only
        ${nghttp2_post_configure_command}
      BUILD_COMMAND ${CMAKE_COMMAND} -E env ${nghttp2_env_args} make -C lib -j${CPKT_DEPENDENCY_BUILD_JOBS}
      INSTALL_COMMAND ${CMAKE_COMMAND} -E env ${nghttp2_env_args} make -C lib install
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS
        "${install_dir}/lib/libnghttp2${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "${install_dir}/lib/libnghttp2${CMAKE_SHARED_LIBRARY_SUFFIX}"
      ${nghttp2_install_byproducts}
      BUILD_IN_SOURCE 0
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
endif()
add_library(cpkt::nghttp2_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::nghttp2_static
    PROPERTIES
      IMPORTED_LOCATION "${install_dir}/lib/libnghttp2${CMAKE_STATIC_LIBRARY_SUFFIX}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
  )
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::nghttp2_static ${project_name})
cpkt_record_dependency_target(${project_name})
else()
cpkt_require_dependency_file("${install_dir}/lib/libnghttp2${CMAKE_STATIC_LIBRARY_SUFFIX}" "nghttp2 (static)")
endif()
add_library(cpkt::nghttp2_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::nghttp2_shared
    PROPERTIES
      IMPORTED_LOCATION "${install_dir}/lib/libnghttp2${CMAKE_SHARED_LIBRARY_SUFFIX}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
  )
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::nghttp2_shared ${project_name})
else()
cpkt_require_dependency_file("${install_dir}/lib/libnghttp2${CMAKE_SHARED_LIBRARY_SUFFIX}" "nghttp2 (shared)")
endif()
set(CPKT_NGHTTP2_static_PREFIX "${install_dir}" PARENT_SCOPE)
set(CPKT_NGHTTP2_shared_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
