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
  message(FATAL_ERROR "Use scripts/package-verify.sh for final archive validation")
endif()
