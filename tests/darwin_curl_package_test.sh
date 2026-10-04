#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  printf 'usage: %s <arm64-apple-darwin validated core SDK prefix>\n' "$0" >&2
  exit 2
fi
if [ "$(uname -s)" != Darwin ]; then
  printf 'Darwin SDK runtime verification requires macOS\n' >&2
  exit 2
fi
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
if [[ -z ${CPKT_OPERATION_FD:-} ]]; then
  exec python3 "$repo_root/scripts/cpkt_operation.py" --group core -- bash "$0" "$@"
fi
python3 "$repo_root/scripts/cpkt_operation.py" --group core --check
prefix=$1
if [[ ! -d "$prefix" ]]; then
  printf 'pass an already validated native core SDK prefix\n' >&2
  exit 2
fi
python3 "$prefix/share/cpkt/validate-sdk.py" --prefix "$prefix" --groups core
work_root="$repo_root/build/verification/arm64-apple-darwin/core/native-package-runtime"
python3 - "$work_root" <<'PYSAFE'
from pathlib import Path
import sys
sys.path.insert(0,'scripts')
from cpkt_packages import safe_owned
safe_owned(Path(sys.argv[1]))
PYSAFE
rm -rf "$work_root"
mkdir -p "$work_root/consumer"
python3 "$repo_root/tests/auth_package_discovery_test.py" "$repo_root" \
  --scratch "$work_root" --compiler "$(xcrun --find clang)" --sdk-prefix "$prefix"
cat > "$work_root/consumer/lua_facade.c" <<'EOF'
#include <cpkt/lua.h>
int main(void) {
  cpkt_lua_state *state = cpkt_lua_l_newstate();
  if (state == 0) return 1;
  cpkt_lua_close(state);
  return 0;
}
EOF
cat > "$work_root/consumer/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(cpkt_darwin_curl_package_test LANGUAGES C)
include(CTest)
find_package(Threads REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(OpenLDAP CONFIG REQUIRED)
find_package(CpktLua CONFIG REQUIRED)
find_package(CpktGssapi CONFIG REQUIRED)
foreach(linkage IN ITEMS static shared)
  add_executable(curl_async_dns_${linkage} "${CPKT_SOURCE_DIR}/tests/curl_async_dns_test.c")
  target_compile_options(curl_async_dns_${linkage} PRIVATE
    -std=c99 -Wall -Wextra -Wpedantic -Werror)
  if(linkage STREQUAL "static")
    target_link_libraries(curl_async_dns_${linkage} PRIVATE CURL::libcurl)
  else()
    target_include_directories(curl_async_dns_${linkage} PRIVATE "${CPKT_SDK_PREFIX}/include")
    target_link_libraries(curl_async_dns_${linkage} PRIVATE
      "${CPKT_SDK_PREFIX}/lib/libcurl.dylib")
    set_target_properties(curl_async_dns_${linkage} PROPERTIES
      BUILD_RPATH "${CPKT_SDK_PREFIX}/lib")
  endif()
  add_test(NAME curl_async_dns_${linkage} COMMAND curl_async_dns_${linkage})
  add_test(NAME curl_multi_socket_${linkage}
    COMMAND "${CPKT_PYTHON3}" "${CPKT_SOURCE_DIR}/tests/curl_async_dns_server.py"
      "$<TARGET_FILE:curl_async_dns_${linkage}>")
  set_tests_properties(curl_async_dns_${linkage} curl_multi_socket_${linkage}
    PROPERTIES TIMEOUT 30)
  add_executable(openldap_${linkage} "${CPKT_SOURCE_DIR}/tests/openldap_link_test.c")
  target_compile_options(openldap_${linkage} PRIVATE
    -std=c89 -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(openldap_${linkage} PRIVATE cpkt::openldap_${linkage})
  add_test(NAME openldap_${linkage} COMMAND openldap_${linkage})
  add_executable(lua_facade_${linkage} lua_facade.c)
  target_compile_options(lua_facade_${linkage} PRIVATE
    -std=c89 -Wall -Wextra -Wpedantic -Werror)
  if(linkage STREQUAL "static")
    target_link_libraries(lua_facade_${linkage} PRIVATE cpkt::lua)
  else()
    target_link_libraries(lua_facade_${linkage} PRIVATE cpkt::lua_facade_shared)
    set_target_properties(openldap_shared lua_facade_shared PROPERTIES
      BUILD_RPATH "${CPKT_SDK_PREFIX}/lib")
  endif()
  add_test(NAME lua_facade_${linkage} COMMAND lua_facade_${linkage})
  add_executable(gssapi_${linkage} "${CPKT_SOURCE_DIR}/tests/gssapi_facade_test.c")
  target_compile_options(gssapi_${linkage} PRIVATE
    -std=c89 -Wall -Wextra -Wpedantic -Werror)
  if(linkage STREQUAL "static")
    target_link_libraries(gssapi_${linkage} PRIVATE cpkt::gssapi)
  else()
    target_link_libraries(gssapi_${linkage} PRIVATE cpkt::gssapi_shared)
    set_target_properties(gssapi_shared PROPERTIES
      BUILD_RPATH "${CPKT_SDK_PREFIX}/lib")
  endif()
  add_test(NAME gssapi_${linkage} COMMAND gssapi_${linkage})
endforeach()
EOF
cmake -S "$work_root/consumer" -B "$work_root/consumer-build" -G Ninja \
  "-DCMAKE_PREFIX_PATH=$prefix" \
  "-DCMAKE_C_COMPILER=$(xcrun --find clang)" \
  "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0" \
  "-DCPKT_SDK_PREFIX=$prefix" \
  "-DCPKT_SOURCE_DIR=$repo_root" \
  "-DCPKT_PYTHON3=$(command -v python3)"
cmake --build "$work_root/consumer-build" --parallel 2
ctest --test-dir "$work_root/consumer-build" --output-on-failure
