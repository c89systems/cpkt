#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
archive=${1:?archive required}
target=${2:?target required}
version=${3:?version required}
smoke=${4:-}
cpkt_preset "$target-release"
if [ "$target" = arm64-apple-darwin ] && [ "$(uname -s)" = Darwin ]; then
  tool=$(xcrun --find otool)
else
  report=$(bash "$cpkt_scripts/cpkt-toolchains.sh" discover "$target")
  if [ "$target" = arm64-apple-darwin ]; then
    tool=$(sed -n 's/^otool=//p' <<< "$report")
  else
    tool=$(sed -n 's/^readelf=//p' <<< "$report")
  fi
fi
[ -x "$tool" ] || cpkt_fail "Missing target inspection tool for $target"
workspace="$cpkt_root/build/package-verify/$target"
cpkt_owned_path "$workspace"
"$cpkt_cmake" -S "$cpkt_root/tests/package" -B "$workspace" -G Ninja \
  "-DCPKT_SOURCE=$cpkt_root" "-DCPKT_ARCHIVE=$archive" "-DCPKT_TARGET=$target" \
  "-DCPKT_VERSION=$version" "-DCPKT_TOOL=$tool" "-DCPKT_SMOKE_ARCHIVE=$smoke"
"$cpkt_cmake" --build "$workspace" --target verify_package
