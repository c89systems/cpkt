cmake_minimum_required(VERSION 3.21)
include("${CPKT_ROOT}/tests/test-command.cmake")
set(work "${CPKT_SCRATCH}/sdk-validation")
file(REMOVE_RECURSE "${work}")
set(prefix "${work}/prefix with spaces")
function(cpkt_fixture_sdk prefix version discovery)
  file(MAKE_DIRECTORY "${prefix}/lib/cmake/CpktSDK" "${prefix}/include" "${prefix}/share/cpkt/packages")
  file(COPY_FILE "${CPKT_ROOT}/cmake/CpktSDKValidate.cmake" "${prefix}/lib/cmake/CpktSDK/Validate.cmake")
  file(WRITE "${prefix}/include/sample.h" "int sample(void);\n")
  file(WRITE "${prefix}/lib/libsample.a" "fixture static library\n")
  file(MAKE_DIRECTORY "${prefix}/lib/pkgconfig")
  set(_stage_root "${prefix}")
  set(_owner core)
  set(CPKT_TARGET_ID x86_64-linux-gnu)
  set(CPKT_BUNDLE_VERSION "${version}")
  set(CPKT_CMOCKA_VERSION 1.1.8)
  include("${discovery}")
  set(files "")
  file(GLOB_RECURSE payload LIST_DIRECTORIES FALSE RELATIVE "${prefix}" "${prefix}/*")
  list(SORT payload)
  foreach(path IN LISTS payload)
    file(CHMOD "${prefix}/${path}" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    file(SHA256 "${prefix}/${path}" sha)
    string(APPEND files "${separator}{\"mode\":\"0644\",\"path\":\"${path}\",\"sha256\":\"${sha}\",\"type\":\"file\"}")
    set(separator ,)
  endforeach()
  set(before "{\"components\":[],\"files\":[${files}],\"group\":\"core\",\"libc\":\"gnu\",\"macos_deployment_target\":null,")
  set(after "\"release_version\":\"${version}\",\"requires_core\":null,\"schema_version\":1,\"target_id\":\"x86_64-linux-gnu\"}")
  string(SHA256 id "${before}${after}")
  set(manifest "${before}\"package_id\":\"${id}\",${after}")
  file(WRITE "${prefix}/share/cpkt/packages/core.json" "${manifest}")
  set(fixture_id "${id}" PARENT_SCOPE)
  set(fixture_manifest "${manifest}" PARENT_SCOPE)
endfunction()
cpkt_fixture_sdk("${prefix}" 0.3.0 "${CPKT_ROOT}/cmake/sdk-discovery.cmake")
set(manifest "${fixture_manifest}")
file(MAKE_DIRECTORY "${work}/source")
file(WRITE "${work}/source/CMakeLists.txt" [=[cmake_minimum_required(VERSION 3.21)
project(sdk_validation NONE)
if(HOST_IMPORT)
  add_library(host_sample STATIC IMPORTED GLOBAL)
  set_target_properties(host_sample PROPERTIES IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/host/libsample.a")
endif()
list(PREPEND CMAKE_PREFIX_PATH "${PREFIX}")
find_package(${PACKAGE} ${REQUEST} ${EXACT} CONFIG REQUIRED)
find_package(CpktCore CONFIG REQUIRED)
if(NOT CpktCore_PREFIX STREQUAL PREFIX OR NOT CpktCore_VERSION STREQUAL EXPECTED_VERSION
    OR NOT CpktCore_COMPATIBLE_SINCE STREQUAL EXPECTED_FLOOR
    OR NOT CpktCore_TARGET_ID STREQUAL "x86_64-linux-gnu")
  message(FATAL_ERROR "Core discovery did not expose validated identity")
endif()
if(OTHER_OWNER)
  cpkt_sdk_validate(db)
endif()
]=])
set(configure "${CMAKE_COMMAND}" -S "${work}/source" -B "${work}/graph" "-DPREFIX=${prefix}"
  -DCMAKE_SYSTEM_NAME=Linux -DPACKAGE=CpktCore -DREQUEST=0.2.0
  -DEXPECTED_VERSION=0.3.0 -DEXPECTED_FLOOR=0.2.0)
cpkt_test_command(${configure} -DCPKT_TARGET_ID=x86_64-linux-gnu)
cpkt_test_failure("Existing target host_sample" ${configure} -DHOST_IMPORT=ON)
cpkt_test_command(${configure} -DHOST_IMPORT=OFF)
cpkt_test_failure("owns only core" ${configure} -DOTHER_OWNER=ON)
cpkt_test_command(${configure} -DOTHER_OWNER=OFF)
cpkt_test_failure("SDK target mismatch" ${configure} -DCPKT_TARGET_ID=aarch64-linux-gnu)
file(WRITE "${prefix}/include/sample.h" "changed\n")
cpkt_test_failure("SDK payload differs" ${configure} -DCPKT_TARGET_ID=x86_64-linux-gnu)
file(WRITE "${prefix}/include/sample.h" "int sample(void);\n")
cpkt_test_command(${configure} -DCPKT_TARGET_ID=x86_64-linux-gnu)
string(REPLACE "0.3.0" "0.3.1" corrupt "${manifest}")
file(WRITE "${prefix}/share/cpkt/packages/core.json" "${corrupt}")
cpkt_test_failure("manifest identity mismatch" ${configure} -DCPKT_TARGET_ID=x86_64-linux-gnu)
file(WRITE "${prefix}/share/cpkt/packages/core.json" "${manifest}")

# The real consumer graph must reject a cached package from a different prefix.
file(COPY "${prefix}/" DESTINATION "${work}/other core SDK")
cpkt_test_failure("Resolved core SDK differs from requested prefix"
  "${CMAKE_COMMAND}" -S "${CPKT_ROOT}/tests/sdk-consumers/native" -B "${work}/wrong-consumer-prefix"
  "-DCPKT_SOURCE=${CPKT_ROOT}" "-DCPKT_PREFIX=${work}/other core SDK"
  "-DCpktCore_DIR=${prefix}/lib/cmake/CpktCore")

# An independent provider discovers and checks core without copying its payload.
set(baseline "${work}/baseline core SDK")
cpkt_fixture_sdk("${baseline}" 0.2.0 "${CPKT_ROOT}/cmake/sdk-discovery.cmake")
set(built_id "${fixture_id}")
set(provider "${work}/provider SDK")
file(MAKE_DIRECTORY "${provider}/lib/cmake/FixtureProvider" "${work}/consumer")
set(provider_config [=[
include(CMakeFindDependencyMacro)
find_dependency(CpktCore 0.2.0 CONFIG REQUIRED)
set(FixtureProvider_BUILT_WITH_CORE_VERSION "0.2.0")
set(FixtureProvider_BUILT_WITH_CORE_PACKAGE_ID "@built_id@")
get_filename_component(FixtureProvider_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
]=])
string(CONFIGURE "${provider_config}" provider_config @ONLY)
file(WRITE "${provider}/lib/cmake/FixtureProvider/FixtureProviderConfig.cmake" "${provider_config}")
file(WRITE "${work}/consumer/CMakeLists.txt" [=[cmake_minimum_required(VERSION 3.21)
project(independent_providers NONE)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_FIND_ROOT_PATH "${CORE}" "${PROVIDER}")
set(CMAKE_PREFIX_PATH "${CORE}" "${PROVIDER}")
find_package(FixtureProvider CONFIG REQUIRED)
if(NOT CpktCore_PREFIX STREQUAL CORE OR NOT FixtureProvider_PREFIX STREQUAL PROVIDER
    OR NOT FixtureProvider_BUILT_WITH_CORE_VERSION STREQUAL "0.2.0")
  message(FATAL_ERROR "Provider installations were merged or misresolved")
endif()
if(CpktCore_VERSION VERSION_GREATER FixtureProvider_BUILT_WITH_CORE_VERSION
    AND CpktCore_PACKAGE_ID STREQUAL FixtureProvider_BUILT_WITH_CORE_PACKAGE_ID)
  message(FATAL_ERROR "Upgraded core identity still claims the provider's build pin")
endif()
]=])
set(discover "${CMAKE_COMMAND}" -S "${work}/consumer")
cpkt_test_command(${discover} -B "${work}/baseline-provider" "-DCORE=${baseline}" "-DPROVIDER=${provider}")
cpkt_test_command(${discover} -B "${work}/independent" "-DCORE=${prefix}" "-DPROVIDER=${provider}")
cpkt_test_failure("CpktCore" ${discover} -B "${work}/missing-core"
  "-DCORE=${work}/missing" "-DPROVIDER=${provider}")

# Exercise real find_package decisions, including range bounds and exact pins.
foreach(package CpktCore CpktCmocka)
  foreach(request 0.2.0 0.2.9 0.3.0 0.2.0...0.3.0 0.2.0...<0.4.0)
    cpkt_test_command(${configure} -B "${work}/${package}-accept-${request}"
      "-DPACKAGE=${package}" "-DREQUEST=${request}")
  endforeach()
  foreach(request 0 0.1.9 0.3.1 0.2.0...<0.3.0 0.2.0...0.2.9 0.1.0...0.4.0)
    cpkt_test_failure("${package}" ${configure} -B "${work}/${package}-reject-${request}"
      "-DPACKAGE=${package}" "-DREQUEST=${request}")
  endforeach()
  cpkt_test_command(${configure} -B "${work}/${package}-exact"
    "-DPACKAGE=${package}" -DREQUEST=0.3.0 -DEXACT=EXACT)
  cpkt_test_failure("${package}" ${configure} -B "${work}/${package}-wrong-exact"
    "-DPACKAGE=${package}" -DREQUEST=0.2.0 -DEXACT=EXACT)
  cpkt_test_command(${configure} -B "${work}/${package}-unversioned"
    "-DPACKAGE=${package}" -DREQUEST=)
endforeach()

# Model a release policy edit: 0.4.0 breaks the 0.2.0 compatibility baseline.
file(READ "${CPKT_ROOT}/cmake/sdk-discovery.cmake" breaking_discovery)
string(REPLACE "set(_core_compatible_since \"0.2.0\")"
  "set(_core_compatible_since \"0.4.0\")" breaking_discovery "${breaking_discovery}")
file(WRITE "${work}/breaking-discovery.cmake" "${breaking_discovery}")
set(breaking "${work}/breaking core SDK")
cpkt_fixture_sdk("${breaking}" 0.4.0 "${work}/breaking-discovery.cmake")
cpkt_test_command(${configure} -B "${work}/breaking-current" "-DPREFIX=${breaking}"
  -DREQUEST=0.4.0 -DEXPECTED_VERSION=0.4.0 -DEXPECTED_FLOOR=0.4.0)
cpkt_test_failure("CpktCore" ${configure} -B "${work}/breaking-old" "-DPREFIX=${breaking}"
  -DREQUEST=0.2.0 -DEXPECTED_VERSION=0.4.0 -DEXPECTED_FLOOR=0.4.0)
cpkt_test_failure("CpktCore" ${discover} -B "${work}/breaking-provider"
  "-DCORE=${breaking}" "-DPROVIDER=${provider}")

set(development "${work}/development core SDK")
cpkt_fixture_sdk("${development}" 0.0.0 "${CPKT_ROOT}/cmake/sdk-discovery.cmake")
cpkt_test_command(${configure} -B "${work}/development-current" "-DPREFIX=${development}"
  -DREQUEST=0.0.0 -DEXPECTED_VERSION=0.0.0 -DEXPECTED_FLOOR=0.0.0)
cpkt_test_failure("CpktCore" ${configure} -B "${work}/development-release" "-DPREFIX=${development}"
  -DREQUEST=0.2.0 -DEXPECTED_VERSION=0.0.0 -DEXPECTED_FLOOR=0.0.0)

file(WRITE "${work}/invalid-floor.cmake"
  "set(_owner core)\nset(_stage_root \"${work}/invalid-floor SDK\")\n"
  "set(CPKT_BUNDLE_VERSION 0.1.9)\nset(CPKT_CMOCKA_VERSION 1.1.8)\n"
  "set(CPKT_TARGET_ID x86_64-linux-gnu)\n"
  "include(\"${CPKT_ROOT}/cmake/sdk-discovery.cmake\")\n")
cpkt_test_failure("compatibility floor exceeds" "${CMAKE_COMMAND}" -P "${work}/invalid-floor.cmake")

file(RENAME "${prefix}" "${work}/relocated core SDK")
file(RENAME "${provider}" "${work}/relocated provider SDK")
cpkt_test_command(${discover} -B "${work}/independent-relocated"
  "-DCORE=${work}/relocated core SDK" "-DPROVIDER=${work}/relocated provider SDK")
