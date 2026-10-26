cmake_minimum_required(VERSION 3.21)
project(cpkt_editor_checks NONE)
enable_testing()
find_program(CLANGD clangd REQUIRED)
include("${CPKT_SOURCE}/cmake/CpktMutationPaths.cmake")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/editor.inputs"
  CONTENT "${CPKT_SOURCE}\n${CPKT_BUILD}\n${CLANGD}\n" @ONLY)
add_test(NAME editor_comments COMMAND "${CMAKE_COMMAND}"
  "-DCPKT_SOURCE=${CPKT_SOURCE}" "-DCPKT_BUILD=${CPKT_BUILD}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/comments_test.cmake")
file(READ "${CPKT_SOURCE}/cmake/components.json" inventory)
string(JSON count LENGTH "${inventory}" hover core)
math(EXPR last "${count}-1")
set(inputs "${CMAKE_BINARY_DIR}/editor.inputs" "${CMAKE_CURRENT_SOURCE_DIR}/comments_test.cmake"
  "${CPKT_BUILD}/compile_commands.json" "${CPKT_SOURCE}/.clangd"
  "${CPKT_SOURCE}/scripts/verify-clangd-surface.sh"
  "${CPKT_SOURCE}/scripts/lifecycle-common.sh")
foreach(index RANGE 0 ${last})
  string(JSON source GET "${inventory}" hover core ${index})
  add_test(NAME editor_${index} COMMAND "${CLANGD}" "--check=${CPKT_SOURCE}/${source}")
  list(APPEND inputs "${CPKT_SOURCE}/${source}")
endforeach()
# Diagnose real GSSAPI inputs without clangd's unrelated per-token refactoring
# self-tests, which fail on GSS_ERROR and declaration-only headers.
foreach(source src/gssapi.c include/cpkt/gssapi.h)
  string(MAKE_C_IDENTIFIER "${source}" name)
  add_test(NAME editor_${name} COMMAND "${CLANGD}"
    "--check=${CPKT_SOURCE}/${source}" --check-locations=false)
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
