function(cpkt_add_cmocka)
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/cmocka")
set(source_dir "${prefix_dir}/src")
set(install_dir "${CPKT_EXTERNAL_ROOT}/cmocka/install")
cpkt_append_common_external_cmake_args(common_cmake_args)
cpkt_get_external_cmake_step_commands(cmake_build_command cmake_install_command "${install_dir}")
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include" "${install_dir}/lib")
cpkt_cached_external_project_add(cpkt_cmocka_static_project
      URL "https://cmocka.org/files/2.0/cmocka-${CPKT_CMOCKA_VERSION}.tar.xz"
      URL_HASH "SHA256=39f92f366bdf3f1a02af4da75b4a5c52df6c9f7e736c7d65de13283f9f0ef416"
      PREFIX "${prefix_dir}/static"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${prefix_dir}/static/build"
      CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${install_dir}
        -DCMAKE_BUILD_TYPE=${CPKT_DEPENDENCY_BUILD_TYPE}
        -DBUILD_SHARED_LIBS=OFF -DUNIT_TESTING=OFF -DWITH_EXAMPLES=OFF
        -DPICKY_DEVELOPER=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        ${common_cmake_args}
      BUILD_COMMAND ${cmake_build_command}
      INSTALL_COMMAND ${cmake_install_command}
      BUILD_BYPRODUCTS "${install_dir}/lib/libcmocka${CMAKE_STATIC_LIBRARY_SUFFIX}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
ExternalProject_Add(cpkt_cmocka_shared_project
      PREFIX "${prefix_dir}/shared"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${prefix_dir}/shared/build"
      DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
      DEPENDS cpkt_cmocka_static_project
      CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${install_dir}
        -DCMAKE_BUILD_TYPE=${CPKT_DEPENDENCY_BUILD_TYPE}
        -DBUILD_SHARED_LIBS=ON -DUNIT_TESTING=OFF -DWITH_EXAMPLES=OFF
        -DPICKY_DEVELOPER=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        ${common_cmake_args}
      BUILD_COMMAND ${cmake_build_command}
      INSTALL_COMMAND ${cmake_install_command}
        COMMAND "${CMAKE_COMMAND}" -DCPKT_PREFIX=${install_dir}
          -DCPKT_SHARED_SUFFIX=${CMAKE_SHARED_LIBRARY_SUFFIX}
          -P "${CMAKE_SOURCE_DIR}/cmake/cmocka_metadata.cmake"
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS "${install_dir}/lib/libcmocka${CMAKE_SHARED_LIBRARY_SUFFIX}")
cpkt_record_dependency_target(cpkt_cmocka_static_project)
cpkt_record_dependency_target(cpkt_cmocka_shared_project)
endif()
foreach(_variant static shared)
if(_variant STREQUAL "static")
set(_type STATIC)
set(_suffix "${CMAKE_STATIC_LIBRARY_SUFFIX}")
else()
set(_type SHARED)
set(_suffix "${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()
add_library(cpkt::cmocka_${_variant} ${_type} IMPORTED GLOBAL)
set_target_properties(cpkt::cmocka_${_variant} PROPERTIES
      IMPORTED_LOCATION "${install_dir}/lib/libcmocka${_suffix}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include")
if(_variant STREQUAL "static")
set_property(TARGET cpkt::cmocka_static PROPERTY INTERFACE_COMPILE_DEFINITIONS CMOCKA_STATIC)
endif()
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::cmocka_${_variant} cpkt_cmocka_${_variant}_project)
else()
cpkt_require_dependency_file("${install_dir}/lib/libcmocka${_suffix}" "cmocka ${_variant}")
endif()
endforeach()
add_library(cpkt::cmocka ALIAS cpkt::cmocka_static)
add_library(cmocka::cmocka ALIAS cpkt::cmocka_shared)
set(CPKT_CMOCKA_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
