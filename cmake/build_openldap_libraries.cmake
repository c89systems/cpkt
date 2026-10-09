if(NOT DEFINED CPKT_OPENLDAP_BUILD_DIR)
  message(FATAL_ERROR "CPKT_OPENLDAP_BUILD_DIR is required")
endif()
if(NOT DEFINED CPKT_OPENLDAP_MAKE_PROGRAM)
  message(FATAL_ERROR "CPKT_OPENLDAP_MAKE_PROGRAM is required")
endif()
if(NOT DEFINED CPKT_OPENLDAP_BUILD_JOBS)
  message(FATAL_ERROR "CPKT_OPENLDAP_BUILD_JOBS is required")
endif()

function(cpkt_openldap_build directory target)
  # OpenLDAP relies on make's built-in C object rules until an optional
  # `make depend` creates explicit rules. Isolate it from parent MAKEFLAGS.
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "MAKEFLAGS="
    "${CPKT_OPENLDAP_MAKE_PROGRAM}" -C "${directory}" "${target}"
    "-j${CPKT_OPENLDAP_BUILD_JOBS}"
    WORKING_DIRECTORY "${CPKT_OPENLDAP_BUILD_DIR}" RESULT_VARIABLE _result)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "OpenLDAP build failed in ${directory} for ${target} (exit ${_result})")
  endif()
endfunction()

# The SDK ships the client libraries, not upstream test programs or utilities.
# Generate the public headers first, then build only the library targets.
cpkt_openldap_build(include all)
cpkt_openldap_build(libraries/liblutil liblutil.a)
cpkt_openldap_build(libraries/liblber liblber.la)
cpkt_openldap_build(libraries/libldap libldap.la)
