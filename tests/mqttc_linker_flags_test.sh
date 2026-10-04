#!/usr/bin/env bash
set -euo pipefail

repo_root=${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
dependencies_cmake="$repo_root/cmake/CpktDependencies.cmake"

if ! grep -F -- 'separate_arguments(mqttc_shared_extra_link_flags NATIVE_COMMAND "${CMAKE_SHARED_LINKER_FLAGS}")' "$dependencies_cmake" >/dev/null 2>&1; then
  printf 'MQTT-C shared link rule does not parse CMAKE_SHARED_LINKER_FLAGS\n' >&2
  exit 1
fi

if ! grep -F -- '${CMAKE_C_COMPILER} ${mqttc_shared_link_flags} ${mqttc_shared_extra_link_flags} -o "${mqttc_shared_library}"' "$dependencies_cmake" >/dev/null 2>&1; then
  printf 'MQTT-C shared link rule does not use CMAKE_SHARED_LINKER_FLAGS\n' >&2
  exit 1
fi
