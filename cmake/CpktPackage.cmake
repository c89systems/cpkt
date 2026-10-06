# Staging is separate from explicit test commands and requires their actual proof.
# This control target is available in every selected graph. Its action scopes
# staging to CPKT_GROUP; the inventory owner "all" describes orchestration.
add_custom_target(package-bundle
  COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/package.sh"
    package-stage --group "${CPKT_GROUP}" --preset "$ENV{CPKT_PRESET}" --scope selected
  VERBATIM)
