# Shared deep ABI/export/rpath/loader/archive helpers and their fixture entrypoints.
include("${CMAKE_CURRENT_LIST_DIR}/package_inspection.cmake")
# Strict facade identifier spelling; type-name substrings are not a C89 test.
set(CPKT_C89_FORBIDDEN_TOKENS
    "stdint\\.h"
    "stdbool\\.h"
    "uint8_t"
    "uint16_t"
    "uint32_t"
    "uint64_t"
    "int8_t"
    "int16_t"
    "int32_t"
    "int64_t"
    "long long"
    "inline")
if(CPKT_ARCHIVE)
  find_program(_python NAMES python3 REQUIRED)
  execute_process(COMMAND "${_python}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/cpkt_archive_assert.py"
    "-DCPKT_ARCHIVE=${CPKT_ARCHIVE}" "-DCPKT_TARGET_ID=${CPKT_TARGET_ID}"
    "-DCPKT_BUNDLE_VERSION=${CPKT_BUNDLE_VERSION}" "-DCPKT_GROUP=${CPKT_GROUP}"
    RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "selected archive assertions failed")
  endif()
endif()
