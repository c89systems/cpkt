#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
[ "$cpkt_owner" != core ] || cpkt_fail 'core has no SDK prerequisite'
target=${1:?target required}
pin=$(python3 "$cpkt_scripts/cpkt_core.py" pin --target "$target")
export GROUP="$cpkt_owner"
cpkt_locked "$target"
if python3 "$cpkt_scripts/cpkt_core.py" validate --target "$target"; then
  "$cpkt_cmake" "-DCPKT_REPO_ROOT=$cpkt_root" "-DCPKT_CORE_TARGET=$target" \
    -DCPKT_CORE_ACTION=link -P "$cpkt_root/cmake/core-dependency.cmake"
  exit 0
fi
workspace="$cpkt_root/build/core-acquisition/$target"
mkdir -p "$workspace"
printf '%s\n' "$pin" > "$workspace/pin.json"
"$cpkt_cmake" "-DCPKT_REPO_ROOT=$cpkt_root" "-DCPKT_CORE_TARGET=$target" \
  "-DCPKT_CORE_PIN=$workspace/pin.json" -DCPKT_CORE_ACTION=acquire \
  -P "$cpkt_root/cmake/core-dependency.cmake"
archive=$(cat "$workspace/archive-path")
stage=$(mktemp -d "$workspace/extract.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
python3 "$cpkt_scripts/cpkt_core.py" extract --target "$target" --archive "$archive" --destination "$stage"
prefix=$(python3 "$cpkt_scripts/cpkt_core.py" extracted-path --target "$target" --destination "$stage")
destination="$cpkt_root/.cache/cpkt/$target/install"
bash "$cpkt_scripts/clean.sh" imported-core --path "$destination"
mkdir -p "$(dirname -- "$destination")"
mv -- "$prefix" "$destination"
python3 "$cpkt_scripts/cpkt_core.py" validate --target "$target"
"$cpkt_cmake" "-DCPKT_REPO_ROOT=$cpkt_root" "-DCPKT_CORE_TARGET=$target" \
  -DCPKT_CORE_ACTION=link -P "$cpkt_root/cmake/core-dependency.cmake"
