function(cpkt_add_zlib)
set(project_name "cpkt_zlib_project")
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/zlib")
set(source_dir "${prefix_dir}/src")
set(build_dir "${prefix_dir}/build")
set(install_dir "${CPKT_EXTERNAL_ROOT}/zlib/install")
set(stamp_dir "${prefix_dir}/stamp")
set(tmp_dir "${prefix_dir}/tmp")
cpkt_append_common_external_cmake_args(common_cmake_args)
cpkt_get_external_cmake_step_commands(cmake_build_command cmake_install_command "${install_dir}")
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include" "${install_dir}/lib")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(zlib_shared_library "${install_dir}/lib/libz.${CPKT_ZLIB_VERSION}${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(zlib_shared_soname "${install_dir}/lib/libz.1${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(zlib_shared_link "${install_dir}/lib/libz${CMAKE_SHARED_LIBRARY_SUFFIX}")
else()
set(zlib_shared_library "${install_dir}/lib/libz${CMAKE_SHARED_LIBRARY_SUFFIX}.${CPKT_ZLIB_VERSION}")
set(zlib_shared_soname "${install_dir}/lib/libz${CMAKE_SHARED_LIBRARY_SUFFIX}.1")
set(zlib_shared_link "${install_dir}/lib/libz${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
set(zlib_static_library "${install_dir}/lib/libz${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(CPKT_BUILD_DEPENDENCIES)
cpkt_cached_external_project_add(${project_name}
      URL
        "https://www.zlib.net/zlib-${CPKT_ZLIB_VERSION}.tar.gz"
        "https://zlib.net/fossils/zlib-${CPKT_ZLIB_VERSION}.tar.gz"
      URL_HASH "SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16"
      DOWNLOAD_NAME "zlib-${CPKT_ZLIB_VERSION}.tar.gz"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${build_dir}"
      STAMP_DIR "${stamp_dir}"
      TMP_DIR "${tmp_dir}"
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      PATCH_COMMAND ${CMAKE_COMMAND}
        -DCPKT_ZLIB_SOURCE_DIR=<SOURCE_DIR>
        -P ${CMAKE_SOURCE_DIR}/cmake/patch_zlib_single_pass.cmake
      CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${install_dir}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=${CPKT_DEPENDENCY_BUILD_TYPE}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DZLIB_BUILD_SHARED=ON
        -DZLIB_BUILD_STATIC=ON
        -DZLIB_BUILD_TESTING=OFF
        -DZLIB_INSTALL=ON
        ${common_cmake_args}
      BUILD_COMMAND ${cmake_build_command}
      INSTALL_COMMAND ${cmake_install_command}
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS
        "${zlib_static_library}"
        "${zlib_shared_library}"
        "${zlib_shared_soname}"
        "${zlib_shared_link}"
      BUILD_IN_SOURCE 0
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
endif()
add_library(cpkt::zlib_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::zlib_static
    PROPERTIES
      IMPORTED_LOCATION "${zlib_static_library}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
  )
add_library(cpkt::zlib_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::zlib_shared
    PROPERTIES
      IMPORTED_LOCATION "${zlib_shared_library}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
  )
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::zlib_static ${project_name})
add_dependencies(cpkt::zlib_shared ${project_name})
cpkt_record_dependency_target(${project_name})
else()
cpkt_require_dependency_file("${zlib_static_library}" "zlib static library")
cpkt_require_dependency_file("${zlib_shared_library}" "zlib shared library")
cpkt_require_dependency_file("${zlib_shared_soname}" "zlib shared-library SONAME")
cpkt_require_dependency_file("${zlib_shared_link}" "zlib shared-library linker symlink")
cpkt_require_dependency_file("${install_dir}/include/zlib.h" "zlib header")
cpkt_require_dependency_file("${install_dir}/include/zconf.h" "zlib configuration header")
endif()
set(CPKT_ZLIB_PREFIX "${install_dir}" PARENT_SCOPE)
set(CPKT_ZLIB_SHARED_LIBRARY "${zlib_shared_library}" PARENT_SCOPE)
endfunction()
