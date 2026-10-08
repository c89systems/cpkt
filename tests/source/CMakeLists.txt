cmake_minimum_required(VERSION 3.21)
project(cpkt_source_checks NONE)
enable_testing()
include("${CPKT_SOURCE}/cmake/CpktMutationPaths.cmake")
cpkt_validate_mutation_paths("${CMAKE_BINARY_DIR}/source")
add_test(NAME source_archive COMMAND "${CMAKE_COMMAND}"
  "-DCPKT_SOURCE=${CPKT_SOURCE}" "-DCPKT_ARCHIVE=${CPKT_ARCHIVE}"
  "-DCPKT_PREFIX=${CMAKE_BINARY_DIR}/source" "-DCPKT_VERSION=${CPKT_VERSION}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/archive_test.cmake")
set_tests_properties(source_archive PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/source.inputs"
  CONTENT "${CPKT_ARCHIVE}\n${CPKT_VERSION}\n${CPKT_SOURCE_MODE}\n${CPKT_DEPENDENCY_CACHE}\n${CPKT_SOURCE_GENERATOR}\n${CPKT_DEPENDENCY_BUILD_JOBS}\n" @ONLY)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/archive.passed"
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${CMAKE_BINARY_DIR}/archive.passed" "${CMAKE_BINARY_DIR}/reconstruction.passed"
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}"
    -R "^source_archive$" --output-on-failure --no-tests=error
  COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/archive.passed"
  DEPENDS "${CPKT_ARCHIVE}" "${CMAKE_BINARY_DIR}/source.inputs"
    "${CMAKE_CURRENT_SOURCE_DIR}/archive_test.cmake"
    "${CPKT_SOURCE}/cmake/extract-release-archive.cmake" "${CPKT_SOURCE}/cmake/gnu_tar.cmake"
  VERBATIM)
set(arguments "")
if(CPKT_SOURCE_MODE STREQUAL "release")
  set(arguments release "${CPKT_ARCHIVE}")
endif()
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/reconstruction.passed"
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${CMAKE_BINARY_DIR}/reconstruction.passed"
  COMMAND "${CMAKE_COMMAND}" -E env "CPKT_DEPENDENCY_CACHE=${CPKT_DEPENDENCY_CACHE}"
    "CPKT_SOURCE_GENERATOR=${CPKT_SOURCE_GENERATOR}" "CPKT_DEPENDENCY_BUILD_JOBS=${CPKT_DEPENDENCY_BUILD_JOBS}"
    bash "${CMAKE_BINARY_DIR}/source/scripts/source-reconstruct.sh" ${arguments}
  COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/reconstruction.passed"
  DEPENDS "${CMAKE_BINARY_DIR}/archive.passed" "${CMAKE_BINARY_DIR}/source.inputs"
  VERBATIM)
add_custom_target(verify_source DEPENDS "${CMAKE_BINARY_DIR}/reconstruction.passed")
