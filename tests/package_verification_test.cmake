cmake_minimum_required(VERSION 3.21)
include("${CPKT_ROOT}/tests/test-command.cmake")
set(work "${CPKT_SCRATCH}/package-verification")
file(REMOVE_RECURSE "${work}")
set(source "${work}/source")
file(MAKE_DIRECTORY "${source}/tests/package" "${source}/scripts" "${source}/cmake")
file(COPY "${CPKT_ROOT}/tests/package/CMakeLists.txt" DESTINATION "${source}/tests/package")
set(inputs cmake/extract-release-archive.cmake cmake/gnu_tar.cmake
  cmake/CpktSDKValidate.cmake tests/privacy_scan.cmake cmake/components.json
  cmake/CpktLocalRuntime.cmake cmake/CpktReadOnlyToolchain.cmake
  cmake/CpktToolchainDiscovery.cmake cmake/toolchains/arm64-apple-darwin.cmake
  scripts/lifecycle-common.sh scripts/mutation-paths.sh scripts/require-host-bash.sh
  scripts/cpkt-toolchains.sh scripts/cpkt-archive-cache.sh
  cmake/lifecycle-info.cmake)
foreach(input IN LISTS inputs)
  file(WRITE "${source}/${input}" "# fixture input\n")
endforeach()
file(COPY "${CPKT_ROOT}/cmake/CpktMutationPaths.cmake" "${CPKT_ROOT}/cmake/package_inspection.cmake"
  DESTINATION "${source}/cmake")
file(WRITE "${work}/archive" "fixture archive\n")
file(WRITE "${source}/tests/package/archive_test.cmake" "file(MAKE_DIRECTORY \"\${CPKT_PREFIX}\")\n")
file(WRITE "${source}/scripts/package-consumers.sh" [=[
printf 'passed\n' >> "${3%/*}/consumer-runs"
]=])
set(graph "${work}/graph")
cpkt_test_command("${CMAKE_COMMAND}" -S "${source}/tests/package" -B "${graph}" -G Ninja
  "-DCPKT_SOURCE=${source}" "-DCPKT_ARCHIVE=${work}/archive" -DCPKT_TARGET=x86_64-linux-gnu
  -DCPKT_VERSION=1.2.3 -DCPKT_TOOL=fixture)
function(verify expected)
  cpkt_test_command("${CMAKE_COMMAND}" --build "${graph}" --target verify_package)
  file(STRINGS "${graph}/consumer-runs" runs)
  list(LENGTH runs count)
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "Incorrect package verification count: ${count} != ${expected}")
  endif()
endfunction()
verify(1)
verify(1)
set(count 1)
foreach(helper scripts/lifecycle-common.sh scripts/cpkt-toolchains.sh)
  file(APPEND "${source}/${helper}" "# changed fixture input\n")
  math(EXPR count "${count}+1")
  verify(${count})
  verify(${count})
endforeach()

file(WRITE "${source}/lookup.cmake" [=[
include("${CMAKE_CURRENT_LIST_DIR}/cmake/package_inspection.cmake")
set(CPKT_OSXCROSS_HOST "")
cpkt_get_osxcross_lookup(host hints)
if(NOT host STREQUAL "arm64-apple-darwin25.4")
  message(FATAL_ERROR "Wrong resolver prefix")
endif()
]=])
foreach(status missing ready)
  file(WRITE "${source}/scripts/cpkt-toolchains.sh"
    "#!/usr/bin/env bash\nprintf '%s\\n' 'cache=/fixture/status=ready/cache' 'status=${status}' 'prefix=arm64-apple-darwin25.4'\n")
  file(CHMOD "${source}/scripts/cpkt-toolchains.sh" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
  set(lookup "${CMAKE_COMMAND}" -E env --unset=CPKT_OSXCROSS_HOST
    "${CMAKE_COMMAND}" -P "${source}/lookup.cmake")
  if(status STREQUAL "missing")
    cpkt_test_failure("not ready" ${lookup})
  else()
    cpkt_test_command(${lookup})
  endif()
endforeach()
