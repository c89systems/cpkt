#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
mode=${1:-smoke}
cpkt_check_group "${GROUP:-all}"
case "$mode" in smoke|standard|long) ;; *) cpkt_fail "unknown fuzz mode: $mode";; esac
[ "$mode" != long ] || [ "${CPKT_FUZZ_LONG_ENABLE:-0}" = 1 ] || cpkt_fail 'long fuzz requires CPKT_FUZZ_LONG_ENABLE=1'
bash "$cpkt_scripts/require-native-hardening-host.sh" afl++
resolved=$(bash "$cpkt_scripts/cpkt-aflpp.sh" env) || exit $?
eval "$resolved"
bash "$cpkt_scripts/build.sh" build --group core --preset fuzz --target cpkt_lua_runtime_fuzz
cpkt_preset fuzz
"$cpkt_cmake" --build "$cpkt_binary" --target "cpkt_verify_fuzz_$mode"
