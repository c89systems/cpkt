function(cpkt_add_lua)
set(project_name "cpkt_lua_project")
set(prefix_dir "${CPKT_DEPENDENCY_BUILD_ROOT}/lua")
set(source_dir "${prefix_dir}/src")
set(install_dir "${CPKT_EXTERNAL_ROOT}/lua/install")
set(stamp_dir "${prefix_dir}/stamp")
set(tmp_dir "${prefix_dir}/tmp")
cpkt_get_strip_dependency_install_command(strip_install_command "${install_dir}")
if(CPKT_BUILD_DEPENDENCIES)
file(MAKE_DIRECTORY "${install_dir}/include" "${install_dir}/lib")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(lua_shared_library "liblua.${CPKT_LUA_VERSION}${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(lua_shared_soname "liblua.5.5${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(lua_shared_link "liblua${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(lua_shared_link_flags -dynamiclib -Wl,-install_name,@rpath/${lua_shared_soname})
set(lua_shared_libs -lm)
else()
set(lua_shared_library "liblua${CMAKE_SHARED_LIBRARY_SUFFIX}.${CPKT_LUA_VERSION}")
set(lua_shared_soname "liblua${CMAKE_SHARED_LIBRARY_SUFFIX}.5.5")
set(lua_shared_link "liblua${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(lua_shared_link_flags -shared -Wl,-soname,${lua_shared_soname})
set(lua_shared_libs -lm)
if(CMAKE_DL_LIBS)
list(APPEND lua_shared_libs "-l${CMAKE_DL_LIBS}")
endif()
endif()
set(lua_static_library "${install_dir}/lib/liblua${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(lua_shared_library_path "${install_dir}/lib/${lua_shared_library}")
cpkt_get_external_c_flags(lua_external_cflags)
set(lua_my_cflags "${lua_external_cflags} -fPIC -DLUA_USE_POSIX -DLUA_USE_DLOPEN")
set(lua_env_args "")
cpkt_append_pinned_external_toolchain_env_args(lua_env_args)
set(lua_shared_extra_link_flags "")
if(CMAKE_SHARED_LINKER_FLAGS)
separate_arguments(lua_shared_extra_link_flags NATIVE_COMMAND "${CMAKE_SHARED_LINKER_FLAGS}")
endif()
set(lua_base_objects
    "${source_dir}/src/lapi.o"
    "${source_dir}/src/lcode.o"
    "${source_dir}/src/lctype.o"
    "${source_dir}/src/ldebug.o"
    "${source_dir}/src/ldo.o"
    "${source_dir}/src/ldump.o"
    "${source_dir}/src/lfunc.o"
    "${source_dir}/src/lgc.o"
    "${source_dir}/src/llex.o"
    "${source_dir}/src/lmem.o"
    "${source_dir}/src/lobject.o"
    "${source_dir}/src/lopcodes.o"
    "${source_dir}/src/lparser.o"
    "${source_dir}/src/lstate.o"
    "${source_dir}/src/lstring.o"
    "${source_dir}/src/ltable.o"
    "${source_dir}/src/ltm.o"
    "${source_dir}/src/lundump.o"
    "${source_dir}/src/lvm.o"
    "${source_dir}/src/lzio.o"
    "${source_dir}/src/lauxlib.o"
    "${source_dir}/src/lbaselib.o"
    "${source_dir}/src/lcorolib.o"
    "${source_dir}/src/ldblib.o"
    "${source_dir}/src/liolib.o"
    "${source_dir}/src/lmathlib.o"
    "${source_dir}/src/loadlib.o"
    "${source_dir}/src/loslib.o"
    "${source_dir}/src/lstrlib.o"
    "${source_dir}/src/ltablib.o"
    "${source_dir}/src/lutf8lib.o"
    "${source_dir}/src/linit.o"
  )
if(CPKT_BUILD_DEPENDENCIES)
cpkt_external_install_byproducts(lua_install_byproducts
      "${install_dir}/include/lua.h"
      "${install_dir}/include/luaconf.h"
      "${install_dir}/include/lauxlib.h"
      "${install_dir}/include/lualib.h"
      "${install_dir}/include/lua.hpp")
cpkt_cached_external_project_add(${project_name}
      URL "https://lua.org/ftp/lua-${CPKT_LUA_VERSION}.tar.gz"
      URL_HASH "SHA256=1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce"
      DOWNLOAD_NAME "lua-${CPKT_LUA_VERSION}.tar.gz"
      PREFIX "${prefix_dir}"
      DOWNLOAD_DIR "${CPKT_DOWNLOAD_ROOT}"
      SOURCE_DIR "${source_dir}"
      STAMP_DIR "${stamp_dir}"
      TMP_DIR "${tmp_dir}"
      TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT}
      INACTIVITY_TIMEOUT ${CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT}
      PATCH_COMMAND ${CMAKE_COMMAND}
        -DCPKT_LUA_SOURCE_DIR=${source_dir}
        -DCPKT_LUA_HOOK_POLICY_HEADER=${CMAKE_SOURCE_DIR}/src/lua_runtime_hook_policy.h
        -P ${CMAKE_SOURCE_DIR}/cmake/patch_lua_runtime_hooks.cmake
      CONFIGURE_COMMAND ${CMAKE_COMMAND} -E true
      BUILD_COMMAND
          ${CMAKE_COMMAND} -E env ${lua_env_args} MAKEFLAGS= make -C "${source_dir}/src" a -j1
            CC=${CMAKE_C_COMPILER}
            AR=${CMAKE_AR}\ rcu
            RANLIB=${CMAKE_RANLIB}
            MYCFLAGS=${lua_my_cflags}
        COMMAND
          ${CMAKE_COMMAND} -E env ${lua_env_args}
          ${CMAKE_C_COMPILER}
          ${lua_shared_link_flags}
          ${lua_shared_extra_link_flags}
          -o "${source_dir}/src/${lua_shared_library}"
          ${lua_base_objects}
          ${lua_shared_libs}
      INSTALL_COMMAND
        ${CMAKE_COMMAND}
          -DCPKT_LUA_SOURCE_DIR=${source_dir}
          -DCPKT_LUA_INSTALL_DIR=${install_dir}
          -DCPKT_LUA_VERSION=${CPKT_LUA_VERSION}
          -DCPKT_LUA_SHARED_LIBRARY=${lua_shared_library}
          -DCPKT_LUA_SHARED_SONAME=${lua_shared_soname}
          -DCPKT_LUA_SHARED_LINK=${lua_shared_link}
          -P ${CMAKE_SOURCE_DIR}/cmake/install_lua.cmake
        COMMAND ${strip_install_command}
      BUILD_BYPRODUCTS
        "${lua_static_library}"
        "${lua_shared_library_path}"
      ${lua_install_byproducts}
      BUILD_IN_SOURCE 1
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
endif()
add_library(cpkt::lua_static STATIC IMPORTED GLOBAL)
set_target_properties(cpkt::lua_static
    PROPERTIES
      IMPORTED_LOCATION "${lua_static_library}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
      INTERFACE_LINK_LIBRARIES "m;${CMAKE_DL_LIBS}"
  )
add_library(cpkt::lua_shared SHARED IMPORTED GLOBAL)
set_target_properties(cpkt::lua_shared
    PROPERTIES
      IMPORTED_LOCATION "${lua_shared_library_path}"
      INTERFACE_INCLUDE_DIRECTORIES "${install_dir}/include"
      INTERFACE_LINK_LIBRARIES "m;${CMAKE_DL_LIBS}"
  )
if(CPKT_BUILD_DEPENDENCIES)
add_dependencies(cpkt::lua_static ${project_name})
add_dependencies(cpkt::lua_shared ${project_name})
cpkt_record_dependency_target(${project_name})
else()
cpkt_require_dependency_file("${lua_static_library}" "Lua static library")
cpkt_require_dependency_file("${lua_shared_library_path}" "Lua shared library")
cpkt_require_dependency_file("${install_dir}/include/lua.h" "Lua header")
cpkt_require_dependency_file("${install_dir}/include/lauxlib.h" "Lua auxiliary header")
cpkt_require_dependency_file("${install_dir}/include/lualib.h" "Lua standard library header")
endif()
set(CPKT_LUA_PREFIX "${install_dir}" PARENT_SCOPE)
endfunction()
