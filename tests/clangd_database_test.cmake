cmake_minimum_required(VERSION 3.21)
include("${CPKT_ROOT}/tests/test-command.cmake")
find_program(CLANGD clangd REQUIRED)
set(work "${CPKT_SCRATCH}/clangd database")
set(source "${work}/source")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${source}/scripts" "${source}/cmake"
  "${source}/src" "${source}/private-includes")
foreach(script build.sh lifecycle-common.sh mutation-paths.sh require-host-bash.sh)
  file(COPY "${CPKT_ROOT}/scripts/${script}" DESTINATION "${source}/scripts")
endforeach()
file(COPY "${CPKT_ROOT}/cmake/lifecycle-info.cmake" DESTINATION "${source}/cmake")
file(COPY "${CPKT_ROOT}/.clangd" DESTINATION "${source}")
file(WRITE "${source}/cmake/components.json" "{\"repository_group\":\"core\"}\n")
file(WRITE "${source}/scripts/cpkt-toolchains.sh" "printf 'status=ready\\n'\n")
file(WRITE "${source}/CMakePresets.json" [=[{
  "version": 3,
  "configurePresets": [
    {"name":"debug","generator":"Ninja","binaryDir":"${sourceDir}/build/native",
     "cacheVariables":{"CMAKE_EXPORT_COMPILE_COMMANDS":"ON"}},
    {"name":"x86_64-linux-musl-release","inherits":"debug"}
  ]
}
]=])
file(WRITE "${source}/CMakeLists.txt" [=[cmake_minimum_required(VERSION 3.21)
project(editor_fixture C)
if(CPKT_DEPENDENCY_PRODUCER)
  add_custom_target(cpkt_deps_all)
  return()
endif()
add_library(editor_fixture OBJECT src/probe.c)
target_include_directories(editor_fixture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private-includes")
target_compile_definitions(editor_fixture PRIVATE EDITOR_VALUE=42)
]=])
file(WRITE "${source}/private-includes/editor_dependency.h"
  "static int editor_value(int value) { return value; }\n")
file(WRITE "${source}/src/probe.c"
  "#include <editor_dependency.h>\nint probe(void) { return editor_value(EDITOR_VALUE); }\n")
set(build bash "${source}/scripts/build.sh" build --group all)
set(database "${source}/build/clangd/compile_commands.json")
cpkt_test_command(${build} --preset debug)
file(READ "${database}" native)
file(READ "${source}/build/x86_64-linux-gnu/core/Debug/compile_commands.json" expected)
if(NOT native STREQUAL expected)
  message(FATAL_ERROR "Native build did not publish its editor database")
endif()
set(check "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${work}/config"
  "${CLANGD}" "--check=${source}/src/probe.c")
cpkt_test_command(${check})

# A different target's build must leave the native editor context selected.
cpkt_test_command(${build} --preset x86_64-linux-musl-release)
file(READ "${database}" after_cross)
if(NOT after_cross STREQUAL native)
  message(FATAL_ERROR "Cross build replaced the native editor database")
endif()
file(REMOVE_RECURSE "${source}/build/clangd")
cpkt_test_command(${build} --preset debug)
cpkt_test_command(${check})

# Exercise the editor's actual discovery path; no explicit database override.
file(WRITE "${source}/.clangd" "CompileFlags:\n  CompilationDatabase: missing\n")
cpkt_test_failure("file not found" ${check})
