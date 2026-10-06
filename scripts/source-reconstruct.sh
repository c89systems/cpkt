#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
[ ! -e "$cpkt_root/.cache" ] && [ ! -e "$cpkt_root/build/x86_64-linux-gnu" ] || cpkt_fail 'source reconstruction requires empty local compiled/install state'
[ -n "${CPKT_DEPENDENCY_CACHE:-}" ] || cpkt_fail 'source reconstruction requires the resolved shared archive cache'
export CPKT_SOURCE_RECONSTRUCTION=1 CPKT_PRESET=x86_64-linux-gnu-release GROUP=all
case "${CPKT_SOURCE_GENERATOR:-Ninja}" in Ninja|'Unix Makefiles') ;; *) cpkt_fail 'unsupported source reconstruction generator' ;; esac
version=$(bash "$cpkt_scripts/release-version.sh" "$cpkt_root")
base="$cpkt_root/build/source-composition"
archive_name="$cpkt_provider-$version-x86_64-linux-gnu.tar.gz"
cpkt_owned_path "$base/$archive_name"
bash "$cpkt_scripts/build.sh" preflight --group all --preset x86_64-linux-gnu-release
bash "$cpkt_scripts/build.sh" test --group all --preset x86_64-linux-gnu-release
bash "$cpkt_scripts/package.sh" package-stage --group "$cpkt_owner" --preset x86_64-linux-gnu-release --scope selected
mkdir -p "$base"
cp -- "$cpkt_root/build/package-stage/x86_64-linux-gnu/$cpkt_owner/archives/$archive_name" "$base/"
python3 "$cpkt_scripts/cpkt_packages.py" compose --group all --preset x86_64-linux-gnu-release --base "$base"
