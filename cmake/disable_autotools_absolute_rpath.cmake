if(NOT DEFINED CPKT_AUTOTOOLS_LIBTOOL OR "${CPKT_AUTOTOOLS_LIBTOOL}" STREQUAL "")
  message(FATAL_ERROR "CPKT_AUTOTOOLS_LIBTOOL is required")
endif()
if(NOT EXISTS "${CPKT_AUTOTOOLS_LIBTOOL}")
  message(FATAL_ERROR "Autotools libtool file does not exist: ${CPKT_AUTOTOOLS_LIBTOOL}")
endif()

file(READ "${CPKT_AUTOTOOLS_LIBTOOL}" _libtool_content)
set(_absolute_rpath_rule [=[hardcode_libdir_flag_spec="\$wl-rpath \$wl\$libdir"]=])
string(FIND "${_libtool_content}" "${_absolute_rpath_rule}" _absolute_rpath_offset)
if(_absolute_rpath_offset EQUAL -1)
  set(_empty_rpath_rule [=[hardcode_libdir_flag_spec=""]=])
  string(FIND "${_libtool_content}" "${_empty_rpath_rule}" _empty_rpath_offset)
  if(_empty_rpath_offset EQUAL -1)
    message(FATAL_ERROR
      "Autotools libtool file has no known absolute rpath rule: ${CPKT_AUTOTOOLS_LIBTOOL}")
  endif()
  return()
endif()
string(REPLACE
  "${_absolute_rpath_rule}"
  "hardcode_libdir_flag_spec=\"\""
  _libtool_content
  "${_libtool_content}")
file(WRITE "${CPKT_AUTOTOOLS_LIBTOOL}" "${_libtool_content}")
