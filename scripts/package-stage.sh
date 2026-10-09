#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
preset=${PRESET:-${CPKT_PRESET:-}}
group=${GROUP:-$cpkt_owner}
while [ "$#" -gt 0 ]; do
  [ "$#" -ge 2 ] || cpkt_fail "missing value for $1"
  case "$1" in --preset) preset=$2 ;; --group) group=$2 ;; *) cpkt_fail "unknown stage option: $1" ;; esac
  shift 2
done
cpkt_check_group "$group"
[ -n "$preset" ] || cpkt_fail 'Release preset required; pass --preset <preset>'
cpkt_preset "$preset"
[ "$cpkt_configuration" = Release ] || cpkt_fail 'SDK packaging requires Release'
export GROUP="$cpkt_owner"
version=$(bash "$cpkt_scripts/release-version.sh" "$cpkt_root")
workspace="$cpkt_root/build/package-stage/$cpkt_target/$cpkt_owner"
prefix="$workspace/$cpkt_provider-$version-$cpkt_target"
archive="$workspace/archives/$cpkt_provider-$version-$cpkt_target.tar.gz"
cpkt_owned_path "$prefix"
cpkt_owned_path "$archive"
cpkt_owned_path "$archive.tmp"
bash "$cpkt_scripts/clean.sh" stage --path "$prefix"
"$cpkt_cmake" --install "$cpkt_binary" --prefix "$prefix"
bash "$cpkt_scripts/archive.sh" "$prefix" "$archive"
