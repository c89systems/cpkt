# Native test reuse and decision checks

This reference validates the skill and illustrates an optional native pattern.
Do not add its fixture or decision checks to every component's pipeline.

Use this small pattern only when a deterministic local CTest suite needs reuse.
CMake owns its prerequisite targets; CTest owns execution. The runner compares
only explicitly declared suite inputs and a local success marker. There is no
test inventory interpreter, per-case receipt schema or second scheduler.

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.21)
project(reuse_example NONE)
include(CTest)
set(CHECK_MODE normal CACHE STRING "Fixture execution mode")
add_test(NAME contract COMMAND "${CMAKE_COMMAND}"
  "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/input.txt"
  "-DCOUNTER=${CMAKE_CURRENT_BINARY_DIR}/runs.txt" "-DMODE=${CHECK_MODE}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/Fixture.cmake")
set(suite_inputs "${CMAKE_CURRENT_SOURCE_DIR}/input.txt"
  "${CMAKE_CURRENT_SOURCE_DIR}/Fixture.cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/RunSuite.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/CTestTestfile.cmake" "${CMAKE_CTEST_COMMAND}")
add_custom_target(check
  COMMAND "${CMAKE_COMMAND}" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}"
    "-DCTEST_TOOL=${CMAKE_CTEST_COMMAND}" "-DINPUTS=${suite_inputs}"
    "-DSTAMP=${CMAKE_CURRENT_BINARY_DIR}/contract.passed"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/RunSuite.cmake"
  VERBATIM)
```

## RunSuite.cmake

```cmake
set(context "")
foreach(input IN LISTS INPUTS)
  if(NOT EXISTS "${input}")
    file(REMOVE "${STAMP}")
    message(FATAL_ERROR "Missing suite input: ${input}")
  endif()
  file(SHA256 "${input}" hash)
  string(APPEND context "${hash}\n")
endforeach()
set(report_file "${TEST_DIR}/contract-results.xml")
if(EXISTS "${STAMP}" AND EXISTS "${report_file}")
  file(SHA256 "${report_file}" report_hash)
  string(SHA256 previous_identity "${context}${report_hash}")
  file(READ "${STAMP}" previous)
  if(previous STREQUAL previous_identity)
    message(STATUS "Reused unchanged contract suite")
    return()
  endif()
endif()
file(REMOVE "${STAMP}" "${report_file}")
execute_process(COMMAND "${CTEST_TOOL}" --test-dir "${TEST_DIR}"
  -R "^contract$" --no-tests=error --stop-on-failure --output-on-failure
  --output-junit "${report_file}"
  RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "Contract suite failed: ${result}")
endif()
file(READ "${report_file}" report)
if(NOT report MATCHES "<testcase[^>]* name=\"contract\"" OR
    report MATCHES "<(skipped|failure|error)([ \t\r\n/>])")
  message(FATAL_ERROR "Required contract case did not complete successfully")
endif()
file(SHA256 "${report_file}" report_hash)
string(SHA256 identity "${context}${report_hash}")
file(WRITE "${STAMP}" "${identity}")
```

`Fixture.cmake` and `input.txt` are ordinary declared test inputs. In an executable
suite, add the tested build targets to `check`'s `DEPENDS`, and their actual binary,
shared-library, runner and data files to `suite_inputs`. Configure relevant modes
and environment into declared inputs. Do not discover/hash an entire repository
or toolchain. If external state cannot be bound reliably, that check is not eligible
for reuse. Keep markers in the owned build tree; they prove only the selected suite
when its inputs still match. Hashes are not a substitute for native prerequisite
checks such as executable usability or declared runtime availability.

CTest exit status alone does not prove required coverage: skipped cases can
return zero. Validate the expected case and reject skipped/failed/error results
in CTest's fresh JUnit report before writing success. The marker also binds that
report's bytes; missing or changed results cannot supply cached success. This
example checks CTest's own case/status fields, not arbitrary product XML.
Its required scope is the single `contract` case. If adapting the selection,
validate the corresponding complete required case set; this marker is not
project-wide readiness.

Explicit rerun: remove that suite's marker with `cmake -E rm -f`, then build
`check`. Direct CTest remains available, but does not manage this marker; remove
the marker before such a rerun. Clean release removes local markers and creates
fresh results. Once changed test work begins, interruption/failure before
publication leaves no current successful marker.

The [executable fixture](../scripts/test-native-test-reuse.sh) exercises these
exact code blocks in an isolated `build/` workspace, without compiling the project.
It is registered with CTest in [tests/CMakeLists.txt](../tests/CMakeLists.txt).
From the repository root, configure that test graph under `build/` with
`cmake -S skills/pkt-systems-cmake-lifecycle/tests -B build/skill-validation -DSKILL_WORKSPACE_ROOT="$PWD"`,
then run `ctest --test-dir build/skill-validation --stop-on-failure --output-on-failure --no-tests=error`.

## Decision checks for skill changes

Review these outcomes against the whole skill; literal wording checks alone do
not establish them. Independent agent evaluation, when available, should receive
the requests and skill without the expected answers.

| Request or condition | Expected decision |
| --- | --- |
| Component without cpkt | Preserve its declared dependencies; do not add cpkt. |
| Python source/resource generator | Allowed as a CMake-declared leaf transformation. |
| Python isolated test fixture | Allowed under CTest; no production pipeline controller. |
| Python configure cache or test scheduler, renamed a validator/generator | Prohibited lifecycle control; use native tools or obtain explicit permission. |
| Product declares lonejson; fixture compares JSON | Product JSON stays with lonejson; native metadata and fixture standard libraries remain allowed. |
| Unchanged deterministic suite | Reuse matching success; execute no test cases. |
| Changed, missing, failed, skipped or partial suite inputs/results | Invalidate affected success; no full-readiness claim from partial checks or CTest exit status alone. |
| Source release | Prove shipped source independently and avoid equivalent duplicate producers; do not impose an all-target workspace layout. |
| Documentation-only edit | Validate documentation; do not run the component matrix. |
