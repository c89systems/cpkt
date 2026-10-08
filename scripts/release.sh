#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
action=${1:?release action required}
[ "${GROUP:-all}" = all ] || cpkt_fail 'release workflows require GROUP=all'
export GROUP=all
run() {
  bash "$cpkt_scripts/lifecycle.sh" "$1" --group all --preset debug --preset-explicit no \
    --scope '' --scope-explicit no --dependency ''
}
case "$action" in
  prerelease) exec bash "$0" release-pipeline ;;
  prerelease-hardening)
    run prerelease
    run fuzz
    exit 0 ;;
  release-final-matrix) exec bash "$0" release ;;
  release-matrix)
    bash "$cpkt_scripts/package.sh" package --group all --scope binary
    scope=binary
    bash "$cpkt_scripts/package.sh" package-checksums --group all --scope "$scope"
    bash "$cpkt_scripts/package.sh" package-verify --group all --scope "$scope"
    exit 0 ;;
  release|release-pipeline|test-all) ;;
  *) cpkt_fail "unsupported release action: $action" ;;
esac
if [ "$action" = release ]; then
  export CPKT_RELEASE_PRODUCTION=1
  run lifecycle-version-contract
  run clean
  run format-check
  run package-source
  version=$(bash "$cpkt_scripts/release-version.sh" "$cpkt_root")
  bash "$cpkt_scripts/source-archive-verify.sh" \
    "$cpkt_root/dist/$cpkt_provider-$version.tar.gz" "$version" --release-build
  exit 0
fi
if [ "$action" != test-all ]; then
  if [ "${CPKT_RELEASE_PRODUCTION:-0}" != 1 ]; then run format; fi
  run format-check
fi
bash "$cpkt_scripts/cpkt-aflpp.sh" ensure
run debug
run clangd-surface
run valgrind
run fuzz-smoke
case "$action" in release) run release-final-matrix ;; release-pipeline) run release-matrix ;; esac
