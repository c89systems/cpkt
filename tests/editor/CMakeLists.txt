cmake_minimum_required(VERSION 3.21)
project(cpkt_editor_checks NONE)
enable_testing()
find_program(CLANGD clangd REQUIRED)
include("${CPKT_SOURCE}/cmake/CpktMutationPaths.cmake")
add_test(NAME editor_comments COMMAND "${CMAKE_COMMAND}"
  "-DCPKT_SOURCE=${CPKT_SOURCE}" "-DCPKT_BUILD=${CPKT_BUILD}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/comments_test.cmake")
file(READ "${CPKT_SOURCE}/cmake/components.json" inventory)
string(JSON count LENGTH "${inventory}" hover core)
math(EXPR last "${count}-1")
set(inputs "${CMAKE_CURRENT_SOURCE_DIR}/comments_test.cmake" "${CPKT_BUILD}/compile_commands.json")
foreach(index RANGE 0 ${last})
  string(JSON source GET "${inventory}" hover core ${index})
  add_test(NAME editor_${index} COMMAND "${CLANGD}" "--check=${CPKT_SOURCE}/${source}"
    "--compile-commands-dir=${CPKT_BUILD}" "--query-driver=${CPKT_COMPILER}")
  list(APPEND inputs "${CPKT_SOURCE}/${source}")
endforeach()
file(GLOB_RECURSE headers CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
  "${CPKT_SOURCE}/include/*" "${CPKT_BUILD}/generated/*/include/*")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/editor.passed"
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${CMAKE_BINARY_DIR}/editor.passed"
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure --stop-on-failure --no-tests=error
  COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/editor.passed"
  DEPENDS ${inputs} ${headers} "${CPKT_SOURCE}/cmake/components.json" VERBATIM)
add_custom_target(verify_editor DEPENDS "${CMAKE_BINARY_DIR}/editor.passed")
