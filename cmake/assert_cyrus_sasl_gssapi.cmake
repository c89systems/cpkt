if(NOT DEFINED CPKT_CYRUS_SASL_BUILD_DIR)
  message(FATAL_ERROR "CPKT_CYRUS_SASL_BUILD_DIR is required")
endif()

set(_config "${CPKT_CYRUS_SASL_BUILD_DIR}/config.h")
set(_lib_makefile "${CPKT_CYRUS_SASL_BUILD_DIR}/lib/Makefile")
set(_plugins_makefile "${CPKT_CYRUS_SASL_BUILD_DIR}/plugins/Makefile")
set(_configure_log "${CPKT_CYRUS_SASL_BUILD_DIR}/config.log")
foreach(_required IN ITEMS "${_config}" "${_lib_makefile}" "${_plugins_makefile}" "${_configure_log}")
  if(NOT EXISTS "${_required}")
    message(FATAL_ERROR "Cyrus SASL configure did not produce ${_required}")
  endif()
endforeach()

file(STRINGS "${_configure_log}" _lib_subdir
  REGEX "^ac_cv_cmu_lib_subdir=")
if(NOT "${_lib_subdir}" STREQUAL "ac_cv_cmu_lib_subdir=lib")
  message(FATAL_ERROR
    "Cyrus SASL must use the bundled lib directory, got: ${_lib_subdir}")
endif()
file(STRINGS "${_configure_log}" _configure_args
  REGEX "^  \\$ .*configure --host=")
if(NOT _configure_args MATCHES "--with-lib-subdir=lib")
  message(FATAL_ERROR "Cyrus SASL configure must pin --with-lib-subdir=lib")
endif()

file(STRINGS "${_config}" _gssapi_enabled
  REGEX "^#define (HAVE_GSSAPI|STATIC_GSSAPIV2) ")
list(LENGTH _gssapi_enabled _gssapi_define_count)
if(NOT _gssapi_define_count EQUAL 2)
  message(FATAL_ERROR "Cyrus SASL configured without built-in GSSAPI")
endif()

file(STRINGS "${_lib_makefile}" _static_objects REGEX "^SASL_STATIC_OBJS =")
if(NOT _static_objects MATCHES "(^| )gssapi[.]o( |$)")
  message(FATAL_ERROR "Cyrus SASL static library omits gssapi.o")
endif()
file(STRINGS "${_plugins_makefile}" _modules REGEX "^SASL_MECHS =")
if(NOT _modules MATCHES "(^| )libgssapiv2[.]la( |$)")
  message(FATAL_ERROR "Cyrus SASL shared plugins omit GSSAPI")
endif()
