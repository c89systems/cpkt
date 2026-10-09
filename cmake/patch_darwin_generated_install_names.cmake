if(NOT DEFINED CPKT_DARWIN_INSTALL_NAME_FILE OR CPKT_DARWIN_INSTALL_NAME_FILE STREQUAL "")
  message(FATAL_ERROR "CPKT_DARWIN_INSTALL_NAME_FILE is required")
endif()

if(NOT EXISTS "${CPKT_DARWIN_INSTALL_NAME_FILE}")
  message(FATAL_ERROR "Darwin install-name metadata file not found: ${CPKT_DARWIN_INSTALL_NAME_FILE}")
endif()

file(READ "${CPKT_DARWIN_INSTALL_NAME_FILE}" _cpkt_install_name_content)
set(_cpkt_original_content "${_cpkt_install_name_content}")

string(REPLACE "-install_name $(libdir)/" "-install_name @rpath/"
  _cpkt_install_name_content "${_cpkt_install_name_content}")
string(REPLACE "-install_name \\$rpath/\\$soname" "-install_name @rpath/\\$soname"
  _cpkt_install_name_content "${_cpkt_install_name_content}")

if(_cpkt_install_name_content STREQUAL _cpkt_original_content)
  message(FATAL_ERROR
    "no Darwin install-name metadata was patched in ${CPKT_DARWIN_INSTALL_NAME_FILE}")
endif()

file(WRITE "${CPKT_DARWIN_INSTALL_NAME_FILE}" "${_cpkt_install_name_content}")
