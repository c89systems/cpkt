function(cpkt_add_libxml2)
set(project_name_shared "cpkt_libxml2_shared_project")
set(project_name_static "cpkt_libxml2_static_project")
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/libxml2")
set(source_dir "${prefix_dir}/src")
set(shared_build_dir "${prefix_dir}/build-shared")
set(static_build_dir "${prefix_dir}/build-static")
set(install_dir "${CPKT_EXTERNAL_ROOT}/libxml2/install")
set(stamp_dir "${prefix_dir}/stamp")
set(tmp_dir "${prefix_dir}/tmp")
cpkt_append_common_external_cmake_args(common_cmake_args)
cpkt_get_external_cmake_configure_command(cmake_configure_command)
cpkt_get_external_cmake_step_commands(cmake_build_command cmake_install_command "${install_dir}")
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
find_package(Iconv REQUIRED)
set(libxml2_static_iconv_link_libraries Iconv::Iconv)
set(libxml2_shared_iconv_link_libraries Iconv::Iconv)
if(CPKT_TARGET_ID STREQUAL "arm64-apple-darwin")
list(APPEND libxml2_static_iconv_link_libraries iconv)
list(APPEND libxml2_shared_iconv_link_libraries iconv)
endif()
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include/libxml2" "${install_dir}/lib")
endif()
set(libxml2_static_library "${install_dir}/lib/libxml2${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(libxml2_shared_library "${install_dir}/lib/libxml2.16${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(libxml2_shared_link "${install_dir}/lib/libxml2${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(libxml2_install_rpath "@loader_path")
set(libxml2_platform_cmake_args
      -DCMAKE_SHARED_LINKER_FLAGS=-liconv)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
set(libxml2_shared_library "${install_dir}/lib/libxml2${CMAKE_SHARED_LIBRARY_SUFFIX}.16.1.4")
set(libxml2_shared_link "${install_dir}/lib/libxml2${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(libxml2_install_rpath "$ORIGIN")
set(libxml2_platform_cmake_args
      -DCMAKE_SHARED_LINKER_FLAGS=-Wl,--enable-new-dtags)
else()
set(libxml2_shared_library "${install_dir}/lib/libxml2${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(libxml2_shared_link "${libxml2_shared_library}")
set(libxml2_install_rpath "")
set(libxml2_platform_cmake_args "")
endif()
set(libxml2_common_cmake_args
    -DCMAKE_INSTALL_PREFIX=${install_dir}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_INSTALL_SYSCONFDIR=/etc
    -DCMAKE_BUILD_TYPE=${CPKT_DEPENDENCY_BUILD_TYPE}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON
    -DCMAKE_INSTALL_RPATH=${libxml2_install_rpath}
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF
    -DCMAKE_BUILD_RPATH=
    -DCMAKE_SKIP_INSTALL_RPATH=OFF
    ${libxml2_platform_cmake_args}
    -DLIBXML2_WITH_CATALOG=ON
    -DLIBXML2_WITH_C14N=ON
    -DLIBXML2_WITH_DEBUG=ON
    -DLIBXML2_WITH_DOCS=OFF
    -DLIBXML2_WITH_HTML=ON
    -DLIBXML2_WITH_HTTP=OFF
    -DLIBXML2_WITH_ICONV=ON
    -DLIBXML2_WITH_ICU=OFF
    -DLIBXML2_WITH_LEGACY=OFF
    -DLIBXML2_WITH_MODULES=ON
    -DLIBXML2_WITH_OUTPUT=ON
    -DLIBXML2_WITH_PATTERN=ON
    -DLIBXML2_WITH_PROGRAMS=OFF
    -DLIBXML2_WITH_PUSH=ON
    -DLIBXML2_WITH_PYTHON=OFF
    -DLIBXML2_WITH_READLINE=OFF
    -DLIBXML2_WITH_READER=ON
    -DLIBXML2_WITH_REGEXPS=ON
    -DLIBXML2_WITH_RELAXNG=ON
    -DLIBXML2_WITH_SAX1=ON
    -DLIBXML2_WITH_SCHEMAS=ON
    -DLIBXML2_WITH_SCHEMATRON=ON
    -DLIBXML2_WITH_TESTS=OFF
    -DLIBXML2_WITH_THREADS=ON
    -DLIBXML2_WITH_THREAD_ALLOC=ON
    -DLIBXML2_WITH_TLS=ON
    -DLIBXML2_WITH_VALID=ON
    -DLIBXML2_WITH_WRITER=ON
    -DLIBXML2_WITH_XINCLUDE=ON
    -DLIBXML2_WITH_XPATH=ON
    -DLIBXML2_WITH_XPTR=ON
    -DLIBXML2_WITH_ZLIB=ON
    -DZLIB_ROOT=${CPKT_ZLIB_PREFIX}
    -DZLIB_DIR=${CPKT_ZLIB_PREFIX}/lib/cmake/zlib
    ${common_cmake_args}
  )
if(CPKT_BUILD_DEPENDENCIES)
cpkt_cached_external_project_add(${project_name_shared}
      URL "https://download.gnome.org/sources/libxml2/2.15/libxml2-${CPKT_LIBXML2_VERSION}.tar.xz"
      URL_HASH "SHA256=98087fd181d9070724f3fbc65c7377db03038eb92bd882374daff44940138821"
      DOWNLOAD_NAME "libxml2-${CPKT_LIBXML2_VERSION}.tar.xz"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${shared_build_dir}"
      STAMP_DIR "${stamp_dir}/shared"
      TMP_DIR "${tmp_dir}"
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      DEPENDS cpkt_zlib_project
      CONFIGURE_COMMAND ${cmake_configure_command}
        -DBUILD_SHARED_LIBS=ON
        ${libxml2_common_cmake_args}
      BUILD_COMMAND ${cmake_build_command}
      INSTALL_COMMAND ${cmake_install_command}
      BUILD_BYPRODUCTS "${libxml2_shared_library}"
      BUILD_IN_SOURCE 0
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
cpkt_cached_external_project_add(${project_name_static}
      URL "https://download.gnome.org/sources/libxml2/2.15/libxml2-${CPKT_LIBXML2_VERSION}.tar.xz"
      URL_HASH "SHA256=98087fd181d9070724f3fbc65c7377db03038eb92bd882374daff44940138821"
      DOWNLOAD_NAME "libxml2-${CPKT_LIBXML2_VERSION}.tar.xz"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${static_build_dir}"
      STAMP_DIR "${stamp_dir}/static"
      TMP_DIR "${tmp_dir}"
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      DEPENDS ${project_name_shared}
      CONFIGURE_COMMAND ${cmake_configure_command}
        -DBUILD_SHARED_LIBS=OFF
        ${libxml2_common_cmake_args}
      BUILD_COMMAND ${cmake_build_command}
      INSTALL_COMMAND ${cmake_install_command}
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS "${libxml2_static_library}"
      BUILD_IN_SOURCE 0
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
endif()
add_library(cpkt::libxml2_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::libxml2_static
    PROPERTIES
      IMPORTED_LOCATION "${libxml2_static_library}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include/libxml2"
      INTERFACE_LINK_LIBRARIES "cpkt::zlib_static;${libxml2_static_iconv_link_libraries};${CMAKE_DL_LIBS};Threads::Threads;m"
  )
add_library(cpkt::libxml2_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::libxml2_shared
    PROPERTIES
      IMPORTED_LOCATION "${libxml2_shared_library}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include/libxml2"
      INTERFACE_LINK_LIBRARIES "cpkt::zlib_shared;${libxml2_shared_iconv_link_libraries};${CMAKE_DL_LIBS};Threads::Threads;m"
  )
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::libxml2_static ${project_name_static})
add_dependencies(cpkt::libxml2_shared ${project_name_shared})
cpkt_record_dependency_target(${project_name_static})
else()
cpkt_require_dependency_file("${libxml2_static_library}" "libxml2 static library")
cpkt_require_dependency_file("${libxml2_shared_library}" "libxml2 shared library")
cpkt_require_dependency_file("${libxml2_shared_link}" "libxml2 shared-library linker symlink")
cpkt_require_dependency_file("${install_dir}/include/libxml2/libxml/parser.h" "libxml2 parser header")
endif()
set(CPKT_LIBXML2_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
