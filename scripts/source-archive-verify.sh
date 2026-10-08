#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
archive=${1:?archive required}
version=${2:?version required}
mode=${3:-}
case "$mode" in ''|--release-build) ;; *) cpkt_fail 'unsupported source mode';; esac
workspace="$cpkt_root/build/source-verification/$version"
cpkt_owned_path "$workspace"
source "$cpkt_scripts/source-environment.sh" "$cpkt_root"
source_mode=smoke
[ "$mode" != --release-build ] || source_mode=release
"$cpkt_cmake" -S "$cpkt_root/tests/source" -B "$workspace" \
  "-DCPKT_SOURCE=$cpkt_root" "-DCPKT_ARCHIVE=$archive" "-DCPKT_VERSION=$version" \
  "-DCPKT_SOURCE_MODE=$source_mode" "-DCPKT_DEPENDENCY_CACHE=$CPKT_DEPENDENCY_CACHE" \
  "-DCPKT_SOURCE_GENERATOR=$CPKT_SOURCE_GENERATOR" "-DCPKT_DEPENDENCY_BUILD_JOBS=$CPKT_DEPENDENCY_BUILD_JOBS"
"$cpkt_cmake" --build "$workspace" --target verify_source
if [ "$mode" = --release-build ]; then
  targets=$(cpkt_info targets)
  for target in $targets; do cp -- "$workspace/source/dist/cpkt-$version-$target.tar.gz" "$cpkt_root/dist/"; done
  for name in "cpkt-$version-arm64-apple-darwin-smoke-test.zip" "cpkt-$version-CHECKSUMS"; do
    cp -- "$workspace/source/dist/$name" "$cpkt_root/dist/"
  done
fi
