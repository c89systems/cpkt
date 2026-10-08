#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
[ "$(uname -s):$(uname -m)" = Darwin:arm64 ] || cpkt_fail 'Darwin runtime verification requires arm64 macOS'
case "${1:-}" in
  test-darwin-native)
    bash "$cpkt_scripts/build.sh" test --group core --preset arm64-apple-darwin-debug
    bash "$cpkt_scripts/package.sh" package --group core --preset arm64-apple-darwin-native --scope selected
    bash "$cpkt_scripts/package.sh" package-checksums --group core --preset arm64-apple-darwin-native --scope selected
    bash "$cpkt_scripts/package.sh" package-verify --group core --preset arm64-apple-darwin-native --scope selected ;;
  test-darwin-sdk)
    archive=${CPKT_DARWIN_SDK_ARCHIVE:?select the exact Darwin SDK archive}
    version=${CPKT_DARWIN_SDK_VERSION:?select its version}
    bash "$cpkt_scripts/package-verify.sh" "$archive" arm64-apple-darwin "$version" ;;
  *) cpkt_fail 'usage: darwin.sh test-darwin-native|test-darwin-sdk' ;;
esac
