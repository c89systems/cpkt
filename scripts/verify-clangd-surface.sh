#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
root=${1:?source required}
binary=${2:?build required}
cpkt_publish_compile_commands "$binary"
workspace="$binary/editor-checks"
cpkt_owned_path "$workspace"
"$cpkt_cmake" -S "$root/tests/editor" -B "$workspace" -G Ninja \
  "-DCPKT_SOURCE=$root" "-DCPKT_BUILD=$binary"
"$cpkt_cmake" --build "$workspace" --target verify_editor
