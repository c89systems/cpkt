#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
[ "${GROUP:-all}" = all ] || cpkt_fail 'Darwin workflows require GROUP=all'
export GROUP=all
cpkt_locked "$@"
case "${1:-}" in
  test-darwin-native)
    [ "$(uname -s):$(uname -m)" = Darwin:arm64 ] || cpkt_fail 'native Darwin verification requires arm64 macOS'
    export GROUP=all
    preset=arm64-apple-darwin-native
    version=$(bash "$cpkt_scripts/release-version.sh" "$cpkt_root")
    cpkt_owned_path "$cpkt_root/dist/$cpkt_provider-$version-arm64-apple-darwin.tar.gz"
    bash "$cpkt_scripts/build.sh" preflight --group all --preset "$preset"
    bash "$cpkt_scripts/build.sh" test --group "$cpkt_owner" --preset "$preset"
    bash "$cpkt_scripts/package.sh" package-stage --group "$cpkt_owner" --preset "$preset" --scope selected
    python3 "$cpkt_scripts/cpkt_packages.py" invalidate --group all
    mkdir -p "$cpkt_root/dist"
    cpkt_owned_path "$cpkt_root/dist/$cpkt_provider-$version-arm64-apple-darwin.tar.gz"
    cp -- "$cpkt_root/build/package-stage/arm64-apple-darwin/$cpkt_owner/archives/$cpkt_provider-$version-arm64-apple-darwin.tar.gz" "$cpkt_root/dist/"
    python3 "$cpkt_scripts/cpkt_packages.py" compose --group all --preset "$preset" --base "$cpkt_root/dist"
    python3 "$cpkt_scripts/cpkt_darwin.py" smoke-zip --version "$version"
    python3 "$cpkt_scripts/cpkt_darwin.py" source-evidence ;;
  test-darwin-sdk)
    base=$(python3 "$cpkt_scripts/cpkt_darwin.py" sdk-input)
    python3 "$cpkt_scripts/cpkt_packages.py" compose --group all --preset arm64-apple-darwin-native --base "$base" --fresh-owned
    python3 "$cpkt_scripts/cpkt_darwin.py" sdk-smoke
    for name in cpkt_abi_smoke_static cpkt_abi_smoke_shared; do
      executable="$cpkt_root/build/darwin-artifact-smoke/darwin-smoke-test/bin/$name"
      codesign --verify --strict "$executable"
      "$executable"
    done
    python3 "$cpkt_scripts/cpkt_darwin.py" sdk-evidence ;;
  *) cpkt_fail 'usage: darwin.sh test-darwin-native|test-darwin-sdk' ;;
esac
