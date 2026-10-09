if(NOT DEFINED CPKT_SOURCE_DIR OR CPKT_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "CPKT_SOURCE_DIR is required")
endif()
if(NOT DEFINED CPKT_TEST_ROOT OR CPKT_TEST_ROOT STREQUAL "")
  message(FATAL_ERROR "CPKT_TEST_ROOT is required")
endif()

set(CPKT_DEPENDENCY_CACHE "${CPKT_TEST_ROOT}/failed/shared/deps")
set(CPKT_DEPENDENCY_CACHE_LOCK_TIMEOUT 5)
set(CPKT_DEPENDENCY_DOWNLOAD_TIMEOUT 1)
set(CPKT_DEPENDENCY_DOWNLOAD_INACTIVITY_TIMEOUT 1)
set(CPKT_DEPENDENCY_DOWNLOAD_RETRIES 2)
include("${CPKT_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake")

cpkt_acquire_dependency_archive(_cpkt_unreachable_archive
  NAME "unreachable.tar.gz"
  SHA256 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
  URLS "file://${CPKT_TEST_ROOT}/missing-unreachable.tar.gz")
