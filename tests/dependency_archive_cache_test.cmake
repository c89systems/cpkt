if(NOT DEFINED CPKT_SOURCE_DIR OR CPKT_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "CPKT_SOURCE_DIR is required")
endif()
if(NOT DEFINED CPKT_TEST_ROOT OR CPKT_TEST_ROOT STREQUAL "")
  message(FATAL_ERROR "CPKT_TEST_ROOT is required")
endif()

set(CPKT_DEPENDENCY_CACHE "${CPKT_TEST_ROOT}/shared/deps")
set(CPKT_DEPENDENCY_CACHE_LOCK_TIMEOUT 5)
set(CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT 5)
set(CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT 5)
set(CPKT_DEPENDENCY_DOWNLOAD_RETRIES 2)
include("${CPKT_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake")

set(_cpkt_input "${CPKT_TEST_ROOT}/fixture.tar.gz")
set(_cpkt_payload_dir "${CPKT_TEST_ROOT}/fixture-payload")
set(_cpkt_local_root "${CPKT_TEST_ROOT}/local/.cache/deps")
file(MAKE_DIRECTORY "${_cpkt_payload_dir}")
file(WRITE "${_cpkt_payload_dir}/fixture-payload.txt" "cpkt dependency archive cache fixture\n")
file(ARCHIVE_CREATE
  OUTPUT "${_cpkt_input}"
  PATHS "fixture-payload.txt"
  FORMAT gnutar
  COMPRESSION GZip
  WORKING_DIRECTORY "${_cpkt_payload_dir}")
file(SHA256 "${_cpkt_input}" _cpkt_expected_sha256)
set(_cpkt_url "file://${_cpkt_input}")

cpkt_acquire_dependency_archive(_cpkt_archive
  NAME "fixture.tar.gz"
  SHA256 "${_cpkt_expected_sha256}"
  URLS "file://${CPKT_TEST_ROOT}/unavailable-fixture.tar.gz"
  SEED_PATHS "${_cpkt_input}")
if(NOT EXISTS "${_cpkt_archive}")
  message(FATAL_ERROR "initial cache miss did not publish an archive")
endif()
file(SHA256 "${_cpkt_archive}" _cpkt_actual_sha256)
if(NOT _cpkt_actual_sha256 STREQUAL _cpkt_expected_sha256)
  message(FATAL_ERROR "initial cache archive checksum did not match")
endif()
file(GLOB _cpkt_partial_archives "${CPKT_DEPENDENCY_CACHE}/archives/sha256/${_cpkt_expected_sha256}/.*.part-*")
if(_cpkt_partial_archives)
  message(FATAL_ERROR "initial cache miss left partial archives: ${_cpkt_partial_archives}")
endif()

file(MAKE_DIRECTORY "${_cpkt_local_root}")
file(COPY_FILE "${_cpkt_archive}" "${_cpkt_local_root}/fixture.tar.gz")
file(REMOVE_RECURSE "${_cpkt_local_root}")
# Hide the origin for the offline hit, then restore the exact pinned bytes.
# Regenerating gzip can change its timestamp and therefore its checksum.
file(RENAME "${_cpkt_input}" "${_cpkt_input}.saved")
cpkt_acquire_dependency_archive(_cpkt_offline_archive
  NAME "fixture.tar.gz"
  SHA256 "${_cpkt_expected_sha256}"
  URLS "${_cpkt_url}")
if(NOT _cpkt_offline_archive STREQUAL _cpkt_archive)
  message(FATAL_ERROR "offline cache hit returned a different archive path")
endif()

file(WRITE "${_cpkt_archive}" "corrupt archive\n")
file(RENAME "${_cpkt_input}.saved" "${_cpkt_input}")
cpkt_acquire_dependency_archive(_cpkt_repaired_archive
  NAME "fixture.tar.gz"
  SHA256 "${_cpkt_expected_sha256}"
  URLS "${_cpkt_url}")
file(SHA256 "${_cpkt_repaired_archive}" _cpkt_repaired_sha256)
if(NOT _cpkt_repaired_sha256 STREQUAL _cpkt_expected_sha256)
  message(FATAL_ERROR "corrupt cache archive was not repaired before reuse")
endif()

file(REMOVE "${_cpkt_archive}")
set(_cpkt_retry_url
  "file://${CPKT_TEST_ROOT}/missing-retry-fixture.tar.gz"
  "${_cpkt_url}")
cpkt_acquire_dependency_archive(_cpkt_retry_archive
  NAME "fixture.tar.gz"
  SHA256 "${_cpkt_expected_sha256}"
  URLS ${_cpkt_retry_url})
file(SHA256 "${_cpkt_retry_archive}" _cpkt_retry_sha256)
if(NOT _cpkt_retry_sha256 STREQUAL _cpkt_expected_sha256)
  message(FATAL_ERROR "retry fallback acquisition did not publish the verified archive")
endif()

message(STATUS "dependency archive cache helper passed")
