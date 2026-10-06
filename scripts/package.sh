#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
action=${1:?package action required}
shift
group=${GROUP:-all}
preset=${PRESET:-debug}
scope=${SCOPE:-}
while [ "$#" -gt 0 ]; do
  [ "$#" -ge 2 ] || cpkt_fail "missing value for $1"
  case "$1" in --group) group=$2 ;; --preset) preset=$2 ;; --scope) scope=$2 ;; *) cpkt_fail "unknown package option: $1" ;; esac
  shift 2
done
cpkt_check_group "$group"
case "$action" in package|package-stage|package-verify|package-checksums|test-install-tree|verify-release-archives|verify-release-privacy) ;; *) cpkt_fail "unknown package action: $action" ;; esac
case "$action" in
  verify-release-archives|verify-release-privacy)
    [ -z "$scope" ] || [ "$scope" = release ] || cpkt_fail "$action requires SCOPE=release"
    scope=release ;;
esac
if [ -z "$scope" ]; then
  if [ "$group" = all ]; then scope=binary; else scope=selected; fi
fi
case "$scope" in selected|binary|release) ;; *) cpkt_fail "unknown artifact scope: $scope" ;; esac
if [ "$group" = all ]; then
  [ "$scope" != selected ] || cpkt_fail 'aggregate packaging rejects selected scope'
else
  [ "$scope" = selected ] || cpkt_fail 'selected packaging requires selected scope'
  cpkt_preset "$preset"
  [ "$cpkt_configuration" = Release ] || cpkt_fail 'selected packaging requires Release'
fi
export GROUP="$group" CPKT_PACKAGE_ACTIVE=1
cpkt_locked "$action" --group "$group" --preset "$preset" --scope "$scope"
validate() { python3 "$cpkt_scripts/cpkt_packages.py" "$@"; }
version=$(bash "$cpkt_scripts/release-version.sh" "$cpkt_root")

if [ "$group" != all ]; then
  case "$action" in
    package)
      bash "$cpkt_scripts/build.sh" test --group "$group" --preset "$preset"
      validate assert-inputs --group "$group" --preset "$preset"
      validate invalidate-selected --group "$group" --preset "$preset"
      bash "$cpkt_scripts/package-stage.sh" --group "$group" --preset "$preset" ;;
    package-stage)
      validate assert-inputs --group "$group" --preset "$preset"
      validate invalidate-selected --group "$group" --preset "$preset"
      bash "$cpkt_scripts/package-stage.sh" --group "$group" --preset "$preset" ;;
  esac
  case "$action" in
    package|package-stage)
      validate verify-selected --group "$group" --preset "$preset"
      validate checksums --group "$group" --preset "$preset" --scope selected ;;
    package-checksums) validate checksums --group "$group" --preset "$preset" --scope selected ;;
    *)
      validate verify-checksums --group "$group" --preset "$preset" --scope selected
      validate verify-selected --group "$group" --preset "$preset" ;;
  esac
  exit 0
fi

if [ "$action" = package-checksums ]; then validate checksums --group all --scope "$scope"; exit 0; fi
if [ "$action" = package ]; then
  targets=()
  while IFS= read -r target; do targets+=("$target"); done < <(cpkt_info targets)
  cpkt_owned_path "$cpkt_root/dist"
  for target in "${targets[@]}"; do
    cpkt_owned_path "$cpkt_root/dist/$cpkt_provider-$version-$target.tar.gz"
  done
  for target in "${targets[@]}"; do
    bash "$cpkt_scripts/build.sh" preflight --group all --preset "$target-release"
  done
  validate invalidate --group all
  mkdir -p "$cpkt_root/dist"
  for target in "${targets[@]}"; do
    selected="$target-release"
    bash "$cpkt_scripts/build.sh" test --group "$cpkt_owner" --preset "$selected"
    bash "$cpkt_scripts/package.sh" package-stage --group "$cpkt_owner" --preset "$selected" --scope selected
    cpkt_owned_path "$cpkt_root/dist/$cpkt_provider-$version-$target.tar.gz"
    cp -- "$cpkt_root/build/package-stage/$target/$cpkt_owner/archives/$cpkt_provider-$version-$target.tar.gz" "$cpkt_root/dist/"
    validate compose --group all --preset "$selected" --base "$cpkt_root/dist"
  done
  python3 "$cpkt_scripts/cpkt_darwin.py" smoke-zip --version "$version"
  exit 0
fi
validate verify-checksums --group all --scope "$scope"
while IFS= read -r target; do
  validate compose --group all --preset "$target-release" --base "$cpkt_root/dist"
done < <(cpkt_info targets)
validate verify-artifacts --group all --scope "$scope"
