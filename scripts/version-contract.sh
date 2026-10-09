#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
[ "${1:-check}" = check ] || cpkt_fail 'usage: version-contract.sh check'
[ -z "${CPKT_RELEASE_VERSION_OVERRIDE:-}" ] || cpkt_fail 'final release rejects candidate version override'
top=$(git -C "$cpkt_root" rev-parse --show-toplevel)
[ "$(CDPATH= cd -- "$top" && pwd -P)" = "$cpkt_root" ] || cpkt_fail 'release requires the owning Git checkout'
[ -z "$(git -C "$cpkt_root" status --porcelain --untracked-files=normal)" ] || cpkt_fail 'release requires a clean committed worktree'
version=$(bash "$cpkt_scripts/release-version.sh" "$cpkt_root")
head=$(git -C "$cpkt_root" rev-parse HEAD)
tag=$(git -C "$cpkt_root" show-ref --verify --hash "refs/tags/v$version") || cpkt_fail 'release requires an exact lightweight vX.Y.Z tag on HEAD'
[ "$head" = "$tag" ] || cpkt_fail 'release tag does not identify HEAD'
git -C "$cpkt_root" verify-commit "$head" || cpkt_fail 'release requires a valid signature on the final amended commit'
printf 'release identity verified: v%s %s\n' "$version" "$head"
