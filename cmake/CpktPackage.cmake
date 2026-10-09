# Native archive output, sharing the already-built and tested Release graph.
function(cpkt_register_package_archive)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    return()
  endif()
  set(_prefix "${CMAKE_SOURCE_DIR}/build/package-stage/${CPKT_TARGET_ID}/core")
  set(_archive "${_prefix}/archives/cpkt-${CPKT_BUNDLE_VERSION}-${CPKT_TARGET_ID}.tar.gz")
  get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  set(_inputs "")
  foreach(_target IN LISTS _targets)
    get_target_property(_type "${_target}" TYPE)
    if(_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY)$")
      list(APPEND _inputs "$<TARGET_FILE:${_target}>")
    endif()
  endforeach()
  file(GLOB_RECURSE _installed CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE "${CPKT_EXTERNAL_ROOT}/*/install/*")
  file(GLOB_RECURSE _public CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
    "${CMAKE_SOURCE_DIR}/include/*" "${CMAKE_SOURCE_DIR}/docs/*" "${CMAKE_SOURCE_DIR}/examples/*")
  add_custom_command(OUTPUT "${_archive}"
    COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/package-stage.sh"
      --group core --preset "${CPKT_TARGET_ID}-release"
    DEPENDS ${_inputs} ${_installed} ${_public}
      "${CMAKE_BINARY_DIR}/cpkt-sdk-install.cmake"
      "${CMAKE_BINARY_DIR}/cpkt-package-components.json"
      "${CMAKE_BINARY_DIR}/verification/runtime.passed"
      "${CMAKE_SOURCE_DIR}/tools/generate_sdk_manifest.py"
      "${CMAKE_SOURCE_DIR}/cmake/package_metadata.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/sdk-discovery.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/CpktSDKValidate.cmake"
      "${CMAKE_SOURCE_DIR}/cmake/payload-ownership.json" "${CMAKE_SOURCE_DIR}/LICENSE"
      "${CMAKE_SOURCE_DIR}/scripts/package-stage.sh"
      "${CMAKE_SOURCE_DIR}/scripts/archive.sh" VERBATIM)
  add_custom_target(package-bundle DEPENDS "${_archive}")
  if(CPKT_TARGET_ID STREQUAL "arm64-apple-darwin")
    set(zip "${CMAKE_SOURCE_DIR}/dist/cpkt-${CPKT_BUNDLE_VERSION}-arm64-apple-darwin-smoke-test.zip")
    cpkt_validate_mutation_paths("${zip}")
    add_custom_command(OUTPUT "${zip}"
      COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/darwin-smoke-package.sh" "${CPKT_BUNDLE_VERSION}"
      DEPENDS "${_archive}" "$<TARGET_FILE:cpkt_abi_smoke_static>" "$<TARGET_FILE:cpkt_abi_smoke_shared>"
        "${CMAKE_SOURCE_DIR}/scripts/darwin-smoke-package.sh" "${CMAKE_SOURCE_DIR}/cmake/smoke-metadata.cmake"
      VERBATIM)
    add_custom_target(package-darwin-smoke DEPENDS "${zip}")
  endif()
endfunction()
cmake_language(DEFER CALL cpkt_register_package_archive)
