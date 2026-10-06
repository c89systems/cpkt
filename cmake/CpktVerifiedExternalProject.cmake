include("${CMAKE_CURRENT_LIST_DIR}/CpktLiteralArguments.cmake")
# ExternalProject owns both cold production and verified warm graph nodes.
# Generated launcher/stamp command changes must not replay compiled installs.
function(cpkt_external_project_add name)
  if(CPKT_VERIFIED_COMPONENT)
    ExternalProject_Add("${name}"
      PREFIX "${CMAKE_BINARY_DIR}/verified-components/${name}"
      SOURCE_DIR "${CPKT_VERIFIED_COMPONENT_INSTALL}"
      DOWNLOAD_COMMAND "" UPDATE_COMMAND "" PATCH_COMMAND ""
      CONFIGURE_COMMAND "" INSTALL_COMMAND ""
      BUILD_ALWAYS ON
      BUILD_COMMAND "${CPKT_HOST_PYTHON_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/scripts/cpkt_receipt_cli.py"
        --root "${CMAKE_SOURCE_DIR}" --group "${CPKT_GROUP}"
        --target "${CPKT_TARGET_ID}" --component "${CPKT_VERIFIED_COMPONENT}"
      DEPENDS ${CPKT_VERIFIED_COMPONENT_DEPENDS})
  else()
    # Forward original ARGV, including empty/list-valued arguments. Expanding
    # ARGN would discard explicit empty commands and split literal semicolons.
    set(_registration "ExternalProject_Add(")
    math(EXPR _last "${ARGC} - 1")
    foreach(_index RANGE 0 ${_last})
      cpkt_literal_argument(_literal "${ARGV${_index}}")
      string(APPEND _registration " ${_literal}")
    endforeach()
    cmake_language(EVAL CODE "${_registration})")
  endif()
endfunction()
