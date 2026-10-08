#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
export GROUP=all CPKT_SOURCE_RECONSTRUCTION=1
if [ "${1:-}" = release ]; then
  [ ! -e "$cpkt_root/.cache" ] || cpkt_fail 'final source reconstruction starts from clean compiled state'
  version=$(bash "$cpkt_scripts/release-version.sh" "$cpkt_root")
  mkdir -p "$cpkt_root/dist"
  cp -- "${2:?source archive required}" "$cpkt_root/dist/cpkt-$version.tar.gz"
  export CPKT_RELEASE_PRODUCTION=1
  bash "$cpkt_scripts/release.sh" release-pipeline
  bash "$cpkt_scripts/package.sh" package-checksums --group all --scope release
  bash "$cpkt_scripts/package.sh" package-verify --group all --scope release
else
  bash "$cpkt_scripts/build.sh" test --group core --preset x86_64-linux-gnu-release
  bash "$cpkt_scripts/package.sh" package-stage --group core --preset x86_64-linux-gnu-release --scope selected
  bash "$cpkt_scripts/package.sh" package-checksums --group core --preset x86_64-linux-gnu-release --scope selected
  bash "$cpkt_scripts/package.sh" package-verify --group core --preset x86_64-linux-gnu-release --scope selected
fi
