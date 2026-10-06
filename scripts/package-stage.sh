#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
preset=${PRESET:?Release preset required}
group=${GROUP:-$cpkt_owner}
while [ "$#" -gt 0 ]; do
  [ "$#" -ge 2 ] || cpkt_fail "missing value for $1"
  case "$1" in --preset) preset=$2 ;; --group) group=$2 ;; *) cpkt_fail "unknown stage option: $1" ;; esac
  shift 2
done
cpkt_check_group "$group"
cpkt_preset "$preset"
[ "$cpkt_configuration" = Release ] || cpkt_fail 'SDK packaging requires Release'
export GROUP="$cpkt_owner"
cpkt_locked --group "$group" --preset "$preset"
version=$(bash "$cpkt_scripts/release-version.sh" "$cpkt_root")
workspace="$cpkt_root/build/package-stage/$cpkt_target/$cpkt_owner"
prefix="$workspace/$cpkt_provider-$version-$cpkt_target"
bash "$cpkt_scripts/clean.sh" stage --path "$prefix"
"$cpkt_cmake" --install "$cpkt_binary" --prefix "$prefix"
python3 "$cpkt_scripts/cpkt_package_manifest.py" --root "$cpkt_root" --group "$cpkt_owner" \
  --target "$cpkt_target" --preset "$preset" --prefix "$prefix" --version "$version"
archive="$workspace/archives/$cpkt_provider-$version-$cpkt_target.tar.gz"
bash "$cpkt_scripts/archive.sh" "$prefix" "$archive"
