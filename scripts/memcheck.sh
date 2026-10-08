#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
group=${GROUP:-all}
regex=
while [ "$#" -gt 0 ]; do
  [ "$#" -ge 2 ] || cpkt_fail "missing value for $1"
  case "$1" in
    --group) group=$2 ;;
    --regex) regex=$2 ;;
    --preset) [ "$2" = valgrind ] || cpkt_fail 'Memcheck requires valgrind' ;;
    *) cpkt_fail "unknown Memcheck option: $1" ;;
  esac
  shift 2
done
cpkt_check_group "$group"
bash "$cpkt_scripts/require-native-hardening-host.sh" valgrind
command -v valgrind >/dev/null || cpkt_fail 'host Valgrind is required'
cpkt_preset debug
[ -f "$cpkt_binary/CMakeCache.txt" ] || cpkt_fail 'prepare native Debug before Memcheck'
if [ -n "$regex" ]; then
  suppression=$(cpkt_info memcheck-suppression)
  if [ -n "$suppression" ]; then suppression="$cpkt_root/$suppression"; fi
  "$cpkt_ctest" -S "$cpkt_root/cmake/memcheck.cmake" "-DCPKT_SOURCE=$cpkt_root" \
    "-DCPKT_BINARY=$cpkt_binary" "-DCPKT_REGEX=$regex" "-DCPKT_SUPPRESSIONS=$suppression"
else
  if [ "${CPKT_TEST_RERUN:-0}" = 1 ]; then
    cpkt_owned_path "$cpkt_binary/verification/memcheck.passed"
    rm -f -- "$cpkt_binary/verification/memcheck.passed"
  fi
  "$cpkt_cmake" --build "$cpkt_binary" --target cpkt_verify_memcheck
fi
