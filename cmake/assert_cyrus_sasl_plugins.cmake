if(NOT DEFINED CPKT_CYRUS_SASL_INSTALL_DIR OR
    NOT DEFINED CPKT_CYRUS_SASL_MODULE_SUFFIX)
  message(FATAL_ERROR "Cyrus SASL install directory and shared suffix are required")
endif()

set(_plugin_dir "${CPKT_CYRUS_SASL_INSTALL_DIR}/lib/sasl2")
if(NOT EXISTS "${_plugin_dir}/libgssapiv2${CPKT_CYRUS_SASL_MODULE_SUFFIX}")
  message(FATAL_ERROR "Installed Cyrus SASL GSSAPI plugin is missing from ${_plugin_dir}")
endif()

# Static consumers use the GSSAPI objects inside libsasl2.a. The separate
# plugin archives and libtool metadata are build products, not SDK interfaces.
file(GLOB _plugin_build_files "${_plugin_dir}/*.a" "${_plugin_dir}/*.la")
if(_plugin_build_files)
  file(REMOVE ${_plugin_build_files})
endif()
