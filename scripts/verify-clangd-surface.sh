#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
root=${1:?source required}
binary=${2:?build required}
[ -f "$binary/compile_commands.json" ] || cpkt_fail 'native compile database is missing'
compiler=$(cpkt_cache_value CMAKE_C_COMPILER "$binary/CMakeCache.txt")
workspace="$binary/editor-checks"
cpkt_owned_path "$workspace"
"$cpkt_cmake" -S "$root/tests/editor" -B "$workspace" -G Ninja \
  "-DCPKT_SOURCE=$root" "-DCPKT_BUILD=$binary" "-DCPKT_COMPILER=$compiler"
"$cpkt_cmake" --build "$workspace" --target verify_editor
cp -- "$binary/compile_commands.json" "$root/build/compile_commands.json"
