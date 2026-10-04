foreach(_required
    CPKT_LUA_SOURCE_DIR
    CPKT_LUA_INSTALL_DIR
    CPKT_LUA_VERSION
    CPKT_LUA_SHARED_LIBRARY
    CPKT_LUA_SHARED_LINK)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

file(MAKE_DIRECTORY
  "${CPKT_LUA_INSTALL_DIR}/include"
  "${CPKT_LUA_INSTALL_DIR}/lib"
)

foreach(_header lua.h luaconf.h lualib.h lauxlib.h lua.hpp)
  file(COPY_FILE
    "${CPKT_LUA_SOURCE_DIR}/src/${_header}"
    "${CPKT_LUA_INSTALL_DIR}/include/${_header}"
  )
endforeach()

file(COPY_FILE
  "${CPKT_LUA_SOURCE_DIR}/src/liblua.a"
  "${CPKT_LUA_INSTALL_DIR}/lib/liblua.a"
)
file(COPY_FILE
  "${CPKT_LUA_SOURCE_DIR}/src/${CPKT_LUA_SHARED_LIBRARY}"
  "${CPKT_LUA_INSTALL_DIR}/lib/${CPKT_LUA_SHARED_LIBRARY}"
)

if(NOT "${CPKT_LUA_SHARED_LINK}" STREQUAL "${CPKT_LUA_SHARED_LIBRARY}")
  file(CREATE_LINK
    "${CPKT_LUA_SHARED_LIBRARY}"
    "${CPKT_LUA_INSTALL_DIR}/lib/${CPKT_LUA_SHARED_LINK}"
    SYMBOLIC
  )
endif()

if(DEFINED CPKT_LUA_SHARED_SONAME AND NOT "${CPKT_LUA_SHARED_SONAME}" STREQUAL "" AND NOT "${CPKT_LUA_SHARED_SONAME}" STREQUAL "${CPKT_LUA_SHARED_LIBRARY}")
  file(CREATE_LINK
    "${CPKT_LUA_SHARED_LIBRARY}"
    "${CPKT_LUA_INSTALL_DIR}/lib/${CPKT_LUA_SHARED_SONAME}"
    SYMBOLIC
  )
endif()
