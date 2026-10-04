# Staging is separate from explicit test commands and requires their actual proof.
if(CPKT_GROUP STREQUAL "all")
  set(_cpkt_package_action stage-target)
else()
  set(_cpkt_package_action stage)
endif()
# This control target is available in every selected graph. Its action scopes
# staging to CPKT_GROUP; the inventory owner "all" describes orchestration.
add_custom_target(package-bundle
  COMMAND "${CPKT_OPERATION_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/cpkt_packages.py"
    "${_cpkt_package_action}" --group "${CPKT_GROUP}" --preset "$ENV{CPKT_PRESET}"
  VERBATIM)
