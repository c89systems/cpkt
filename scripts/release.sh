#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
action=${1:?release action required}
[ "${GROUP:-all}" = all ] || cpkt_fail 'release workflows require GROUP=all'
export GROUP=all
cpkt_locked "$@"
run() {
  bash "$cpkt_scripts/lifecycle.sh" "$1" --group all --preset debug --preset-explicit no \
    --scope '' --scope-explicit no --dependency ''
}
case "$action" in
  prerelease) exec bash "$0" release-pipeline ;;
  prerelease-hardening)
    run prerelease
    if [ "$cpkt_owner" != db ]; then run fuzz; fi
    exit 0 ;;
  release-matrix|release-final-matrix)
    if [ "$action" = release-final-matrix ]; then export CPKT_RELEASE_PRODUCTION=1; fi
    bash "$cpkt_scripts/package.sh" package --group all --scope binary
    if [ "$action" = release-final-matrix ]; then
      run package-source-smoke
      scope=release
    else
      scope=binary
    fi
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
fi
if [ "$action" != test-all ]; then run format; run format-check; fi
bash "$cpkt_scripts/build.sh" preflight --group all --preset debug
run debug
if [ "$cpkt_owner" = db ]; then run e2e-postgres; fi
run clangd-surface
run valgrind
if [ "$cpkt_owner" != db ]; then run fuzz-smoke; fi
case "$action" in release) run release-final-matrix ;; release-pipeline) run release-matrix ;; esac
