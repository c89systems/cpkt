# Test-only release binaries. Never add this target/prefix to SDK dependencies,
# install rules, package metadata, or the release dependency contract.
include("${CMAKE_CURRENT_LIST_DIR}/CpktDependencyArchiveCache.cmake")

function(cpkt_add_test_pslog)
  set(version "0.11.0")
  set(hash_x86_64_linux_gnu "8aaddab3fab38559646cfed315d2a0f15dc5c0e838fa635e4e3186550ad8ef78")
  set(hash_x86_64_linux_musl "42954b17df47bb364f3c7197f875fc14222a3c01fa263ef299fb69f1d7757f43")
  set(hash_aarch64_linux_gnu "ce1af6ae7826e7c1526bddc0853ef8d0392471ec1d5801f98c9c3caba868a447")
  set(hash_aarch64_linux_musl "59af307ae7edca20bb2e6f44d69f70881c586ebf6c255e73d379a5c3dc3ff75e")
  set(hash_armhf_linux_gnu "9d8920b46760393b110672bc0238b6ca739b1d1fde5b2f26ee142f237809f1e9")
  set(hash_armhf_linux_musl "5993dcfb750b70317e583aee372a01b9377728e7be3729feddaf3ea5f88ce3c6")
  set(hash_arm64_apple_darwin "9fa5c06f7124d2d9643472a3949938083673154f58c46fb448f9085b30b4aa4a")
  string(REPLACE "-" "_" key "${CPKT_TARGET_ID}")
  if(NOT DEFINED hash_${key})
    message(FATAL_ERROR "No test-only libpslog binary for ${CPKT_TARGET_ID}")
  endif()
  set(name "libpslog-${version}-${CPKT_TARGET_ID}")
  cpkt_initialize_dependency_cache()
  # Reuse verified archives from the former local cache without downloading
  # them again. Extraction and imported targets remain build-local.
  set(legacy_archive "${CMAKE_SOURCE_DIR}/.cache/test-dependencies/archives/sha256/${hash_${key}}/${name}.tar.gz")
  cpkt_acquire_dependency_archive(archive
    NAME "${name}.tar.gz" SHA256 "${hash_${key}}"
    URLS "https://github.com/sa6mwa/libpslog/releases/download/v${version}/${name}.tar.gz"
    SEED_PATHS "${legacy_archive}")
  set(root "${CMAKE_BINARY_DIR}/test-dependencies")
  file(MAKE_DIRECTORY "${root}")
  file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${root}")
  set(prefix "${root}/${name}")
  if(NOT EXISTS "${prefix}/include/pslog.h" OR
      NOT EXISTS "${prefix}/lib/libpslog.a")
    message(FATAL_ERROR "Invalid libpslog test archive: ${archive}")
  endif()
  add_library(cpkt_test_pslog STATIC IMPORTED)
  set_target_properties(cpkt_test_pslog PROPERTIES
    IMPORTED_LOCATION "${prefix}/lib/libpslog.a"
    INTERFACE_INCLUDE_DIRECTORIES "${prefix}/include"
    INTERFACE_LINK_LIBRARIES Threads::Threads)
endfunction()
