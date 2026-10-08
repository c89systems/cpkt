#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
action=${1:?package action required}; shift
group=${GROUP:-all}
preset=${PRESET:-debug}
scope=${SCOPE:-}
while [ "$#" -gt 0 ]; do
  [ "$#" -ge 2 ] || cpkt_fail "missing value for $1"
  case "$1" in --group) group=$2;; --preset) preset=$2;; --scope) scope=$2;; *) cpkt_fail "unknown package option: $1";; esac
  shift 2
done
cpkt_check_group "$group"
case "$action" in package|package-stage|package-verify|package-checksums|test-install-tree|verify-release-archives|verify-release-privacy) ;; *) cpkt_fail "unknown package action: $action";; esac
case "$scope" in ''|selected|binary|release) ;; *) cpkt_fail "unknown artifact scope: $scope";; esac
version=$(bash "$cpkt_scripts/release-version.sh" "$cpkt_root")
if [ "$group" = core ]; then
  [ -z "$scope" ] || [ "$scope" = selected ] || cpkt_fail 'selected package requires SCOPE=selected'
  cpkt_preset "$preset"
  [ "$cpkt_configuration" = Release ] || cpkt_fail 'selected package requires Release'
  targets=("$cpkt_target")
  base="$cpkt_root/build/package-stage/$cpkt_target/core/archives"
  scope=selected
else
  [ "$scope" != selected ] || cpkt_fail 'aggregate package rejects selected scope'
  scope=${scope:-binary}
  targets=()
  while IFS= read -r target; do targets+=("$target"); done < <(cpkt_info targets)
  base="$cpkt_root/dist"
fi
cpkt_owned_path "$base"
mkdir -p "$base"
files=()
for target in "${targets[@]}"; do files+=("cpkt-$version-$target.tar.gz"); done
if [ "$group" = all ]; then
  files+=("cpkt-$version-arm64-apple-darwin-smoke-test.zip")
  if [ "$scope" = release ]; then files+=("cpkt-$version.tar.gz"); fi
fi
manifest="$base/cpkt-$version-CHECKSUMS"
if [ "$scope" = binary ]; then
  manifest="$cpkt_root/build/package-manifests/cpkt-$version-binary-CHECKSUMS"
fi
case "$action" in
  package|package-stage)
    for target in "${targets[@]}"; do
      selected="$target-release"
      if [ "$action" = package ]; then
        bash "$cpkt_scripts/build.sh" test --group core --preset "$selected"
      fi
      cpkt_preset "$selected"
      "$cpkt_cmake" --build "$cpkt_binary" --target package-bundle
      if [ "$group" = all ]; then
        cp -- "$cpkt_root/build/package-stage/$target/core/archives/cpkt-$version-$target.tar.gz" "$base/"
      fi
    done
    if [ "$group" = all ]; then
      cpkt_preset arm64-apple-darwin-release
      "$cpkt_cmake" --build "$cpkt_binary" --target package-darwin-smoke
    fi
    ;;
  package-checksums)
    cpkt_owned_path "$manifest"
    mkdir -p "${manifest%/*}"
    temporary="$manifest.tmp"
    trap 'rm -f -- "$temporary"' EXIT
    for name in "${files[@]}"; do [ -f "$base/$name" ] || cpkt_fail "missing artifact: $name"; done
    (cd "$base"; for name in "${files[@]}"; do
      if command -v sha256sum >/dev/null; then sha256sum "$name"; else shasum -a 256 "$name"; fi
    done) > "$temporary"
    mv -- "$temporary" "$manifest"
    ;;
  *)
    [ -f "$manifest" ] || cpkt_fail 'generate the complete scoped checksum inventory first'
    "$cpkt_cmake" "-DCPKT_BASE=$base" "-DCPKT_MANIFEST=$manifest" "-DCPKT_FILES=$(IFS=';'; printf '%s' "${files[*]}")" \
      -P "$cpkt_root/cmake/verify-checksums.cmake"
    for target in "${targets[@]}"; do
      smoke=
      if [ "$group:$target" = all:arm64-apple-darwin ]; then
        smoke="$base/cpkt-$version-arm64-apple-darwin-smoke-test.zip"
      fi
      bash "$cpkt_scripts/package-verify.sh" "$base/cpkt-$version-$target.tar.gz" "$target" "$version" "$smoke"
    done
    ;;
esac
