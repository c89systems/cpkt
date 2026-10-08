function(cpkt_add_mqttc)
set(project_name "cpkt_mqttc_project")
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/mqtt-c")
set(source_dir "${prefix_dir}/src")
set(build_dir "${prefix_dir}/build")
set(install_dir "${CPKT_EXTERNAL_ROOT}/mqtt-c/install")
set(stamp_dir "${prefix_dir}/stamp")
set(tmp_dir "${prefix_dir}/tmp")
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include" "${install_dir}/lib" "${build_dir}")
endif()
set(mqttc_static_library "${install_dir}/lib/libmqttc${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(mqttc_shared_library_name "libmqttc.${CPKT_MQTTC_VERSION}${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(mqttc_shared_soname "libmqttc.1${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(mqttc_shared_link "libmqttc${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(mqttc_shared_library "${install_dir}/lib/${mqttc_shared_library_name}")
set(mqttc_shared_link_flags
      -dynamiclib
      -Wl,-install_name,@rpath/${mqttc_shared_soname}
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
set(mqttc_shared_library_name "libmqttc${CMAKE_SHARED_LIBRARY_SUFFIX}.${CPKT_MQTTC_VERSION}")
set(mqttc_shared_soname "libmqttc${CMAKE_SHARED_LIBRARY_SUFFIX}.1")
set(mqttc_shared_link "libmqttc${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(mqttc_shared_library "${install_dir}/lib/${mqttc_shared_library_name}")
set(mqttc_shared_link_flags
      -shared
      -Wl,--enable-new-dtags
      -Wl,-rpath,\$ORIGIN
      -Wl,-soname,${mqttc_shared_soname}
    )
else()
set(mqttc_shared_library_name "libmqttc${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(mqttc_shared_soname "")
set(mqttc_shared_link "")
set(mqttc_shared_library "${install_dir}/lib/${mqttc_shared_library_name}")
set(mqttc_shared_link_flags -shared)
endif()
cpkt_get_external_c_flags(mqttc_external_cflags)
separate_arguments(mqttc_compile_flags NATIVE_COMMAND "${mqttc_external_cflags}")
list(APPEND mqttc_compile_flags -fPIC -I "${source_dir}/include")
set(mqttc_env_args "")
cpkt_append_pinned_external_toolchain_env_args(mqttc_env_args)
set(mqttc_shared_extra_link_flags "")
if(CMAKE_SHARED_LINKER_FLAGS)
separate_arguments(mqttc_shared_extra_link_flags NATIVE_COMMAND "${CMAKE_SHARED_LINKER_FLAGS}")
endif()
set(mqttc_link_libraries Threads::Threads)
set(mqttc_link_flags "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
list(APPEND mqttc_link_flags -pthread)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
list(APPEND mqttc_link_flags -pthread)
endif()
if(CPKT_BUILD_DEPENDENCIES)
cpkt_external_install_byproducts(mqttc_install_byproducts
      "${install_dir}/include/mqtt.h")
cpkt_cached_external_project_add(${project_name}
      URL "https://github.com/LiamBindle/MQTT-C/archive/${CPKT_MQTTC_COMMIT}.tar.gz"
      URL_HASH "SHA256=985898405912dbddf50d8b446226763696e6390fbd6f38b66cede6f38e703086"
      DOWNLOAD_NAME "mqtt-c-${CPKT_MQTTC_COMMIT}.tar.gz"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      BINARY_DIR "${build_dir}"
      STAMP_DIR "${stamp_dir}"
      TMP_DIR "${tmp_dir}"
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      CONFIGURE_COMMAND ${CMAKE_COMMAND} -E make_directory "${build_dir}" "${install_dir}/include" "${install_dir}/lib"
      BUILD_COMMAND
        ${CMAKE_COMMAND} -E copy_directory "${source_dir}/include" "${install_dir}/include"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${build_dir}"
        COMMAND ${CMAKE_COMMAND} -E env ${mqttc_env_args}
          ${CMAKE_C_COMPILER} ${mqttc_compile_flags} -c "${source_dir}/src/mqtt.c" -o "${build_dir}/mqtt.c.o"
        COMMAND ${CMAKE_COMMAND} -E env ${mqttc_env_args}
          ${CMAKE_C_COMPILER} ${mqttc_compile_flags} -c "${source_dir}/src/mqtt_pal.c" -o "${build_dir}/mqtt_pal.c.o"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${mqttc_static_library}"
        COMMAND ${CMAKE_AR} qc "${mqttc_static_library}" "${build_dir}/mqtt.c.o" "${build_dir}/mqtt_pal.c.o"
        COMMAND ${CMAKE_RANLIB} "${mqttc_static_library}"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${mqttc_shared_library}"
        COMMAND ${CMAKE_COMMAND} -E env ${mqttc_env_args}
          ${CMAKE_C_COMPILER} ${mqttc_shared_link_flags} ${mqttc_shared_extra_link_flags} -o "${mqttc_shared_library}" "${build_dir}/mqtt.c.o" "${build_dir}/mqtt_pal.c.o" ${mqttc_link_flags}
      INSTALL_COMMAND
        ${CMAKE_COMMAND} -E copy_directory "${source_dir}/src" "${install_dir}/share/cpkt/mqtt-c/src"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${source_dir}/include" "${install_dir}/share/cpkt/mqtt-c/include"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${install_dir}/lib/${mqttc_shared_soname}" "${install_dir}/lib/${mqttc_shared_link}"
        COMMAND ${CMAKE_COMMAND} -E create_symlink "${mqttc_shared_library_name}" "${install_dir}/lib/${mqttc_shared_soname}"
        COMMAND ${CMAKE_COMMAND} -E create_symlink "${mqttc_shared_soname}" "${install_dir}/lib/${mqttc_shared_link}"
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS
        "${mqttc_static_library}"
        "${mqttc_shared_library}"
      ${mqttc_install_byproducts}
      BUILD_IN_SOURCE 0
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
endif()
add_library(cpkt::mqttc_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::mqttc_static
    PROPERTIES
      IMPORTED_LOCATION "${mqttc_static_library}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
      INTERFACE_LINK_LIBRARIES "${mqttc_link_libraries}"
  )
add_library(cpkt::mqttc_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::mqttc_shared
    PROPERTIES
      IMPORTED_LOCATION "${mqttc_shared_library}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
      INTERFACE_LINK_LIBRARIES "${mqttc_link_libraries}"
  )
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::mqttc_static ${project_name})
add_dependencies(cpkt::mqttc_shared ${project_name})
cpkt_record_dependency_target(${project_name})
else()
cpkt_require_dependency_file("${mqttc_static_library}" "MQTT-C static library")
cpkt_require_dependency_file("${mqttc_shared_library}" "MQTT-C shared library")
cpkt_require_dependency_file("${install_dir}/include/mqtt.h" "MQTT-C header")
cpkt_require_dependency_file("${install_dir}/include/mqtt_pal.h" "MQTT-C PAL header")
endif()
set(CPKT_MQTTC_SOURCE_DIR "${install_dir}/share/cpkt/mqtt-c" PARENT_SCOPE)
set(CPKT_MQTTC_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
