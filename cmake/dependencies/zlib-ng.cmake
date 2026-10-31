function(cpkt_add_zlib_ng)
set(project_name "cpkt_zlib_ng_project")
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/zlib-ng")
set(install_dir "${CPKT_EXTERNAL_ROOT}/zlib-ng/install")
cpkt_append_common_external_cmake_args(common_cmake_args)
if(CPKT_TARGET_ARCH STREQUAL "armhf")
# Compile-only probes cannot distinguish musl's hard-float ABI from softfp.
# Keep the dependency on the ABI selected by the declared ARM toolchain.
list(TRANSFORM common_cmake_args APPEND " -mfloat-abi=hard" REGEX "^-DCMAKE_C_FLAGS=")
endif()
cpkt_get_external_cmake_step_commands(cmake_build_command cmake_install_command "${install_dir}")
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
set(static_library "${install_dir}/lib/libz-ng${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(shared_library "${install_dir}/lib/libz-ng.${CPKT_ZLIB_NG_VERSION}.dylib")
set(shared_soname "${install_dir}/lib/libz-ng.2.dylib")
else()
set(shared_library "${install_dir}/lib/libz-ng.so.${CPKT_ZLIB_NG_VERSION}")
set(shared_soname "${install_dir}/lib/libz-ng.so.2")
endif()
set(shared_link "${install_dir}/lib/libz-ng${CMAKE_SHARED_LIBRARY_SUFFIX}")
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include" "${install_dir}/lib")
cpkt_cached_external_project_add(${project_name}
  URL "https://github.com/zlib-ng/zlib-ng/archive/${CPKT_ZLIB_NG_VERSION}/zlib-ng-${CPKT_ZLIB_NG_VERSION}.tar.gz"
  URL_HASH "SHA256=f9c65aa9c852eb8255b636fd9f07ce1c406f061ec19a2e7d508b318ca0c907d1"
  DOWNLOAD_NAME "zlib-ng-${CPKT_ZLIB_NG_VERSION}.tar.gz"
  PREFIX "${prefix_dir}"
  DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
  SOURCE_DIR "${prefix_dir}/src"
  BINARY_DIR "${prefix_dir}/build"
  STAMP_DIR "${prefix_dir}/stamp"
  TMP_DIR "${prefix_dir}/tmp"
  TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
  INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
  PATCH_COMMAND "${CMAKE_COMMAND}"
    -DCPKT_PATCH_WORKING_DIRECTORY=<SOURCE_DIR>
    -DCPKT_PATCH_SERIES=${CMAKE_SOURCE_DIR}/cmake/patches/zlib-ng.series
    -P ${CMAKE_SOURCE_DIR}/cmake/apply_patch_series.cmake
  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=${install_dir}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_BUILD_TYPE=${CPKT_DEPENDENCY_BUILD_TYPE}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_C_STANDARD=11
    -DZLIB_COMPAT=OFF
    -DZLIB_ALIASES=OFF
    -DWITH_GZFILEOP=ON
    -DBUILD_TESTING=OFF
    -DWITH_GTEST=OFF
    -DWITH_BENCHMARKS=OFF
    -DWITH_NATIVE_INSTRUCTIONS=OFF
    -DWITH_RUNTIME_CPU_DETECTION=ON
    ${common_cmake_args}
  BUILD_COMMAND ${cmake_build_command}
  INSTALL_COMMAND ${cmake_install_command} COMMAND ${strip_install_command}
  BUILD_BYPRODUCTS "${static_library}" "${shared_library}" "${shared_soname}" "${shared_link}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
cpkt_record_dependency_target(${project_name})
endif()
foreach(variant static shared)
if(variant STREQUAL "static")
set(kind STATIC)
set(location "${static_library}")
else()
set(kind SHARED)
set(location "${shared_library}")
endif()
add_library(cpkt::zlib_ng_${variant} ${kind} IMPORTED GLOBAL)
set_target_properties(cpkt::zlib_ng_${variant} PROPERTIES
  IMPORTED_LOCATION "${location}"
  INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include")
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::zlib_ng_${variant} ${project_name})
else()
cpkt_require_dependency_file("${location}" "zlib-ng ${variant} library")
endif()
endforeach()
if(NOT CPKT_BUILD_DEPENDENCIES)
cpkt_require_dependency_file("${install_dir}/include/zlib-ng.h" "zlib-ng public header")
cpkt_require_dependency_file("${install_dir}/include/zconf-ng.h" "zlib-ng configuration header")
endif()
set(CPKT_ZLIB_NG_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
