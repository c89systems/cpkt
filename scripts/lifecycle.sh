#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"

[ "$#" -gt 0 ] || cpkt_fail 'a lifecycle action is required'
action=$1
shift
group=${GROUP:-all}
preset=${PRESET:-debug}
preset_explicit=no
scope=${SCOPE:-}
scope_explicit=no
dependency=${DEPENDENCY:-}
if [ "${PRESET+x}" ]; then preset_explicit=yes; fi
if [ "${SCOPE+x}" ]; then scope_explicit=yes; fi
while [ "$#" -gt 0 ]; do
  [ "$#" -ge 2 ] || cpkt_fail "missing value for $1"
  case "$1" in
    --group) group=$2 ;;
    --preset) preset=$2 ;;
    --preset-explicit) preset_explicit=$2 ;;
    --scope) scope=$2 ;;
    --scope-explicit) scope_explicit=$2 ;;
    --dependency) dependency=$2 ;;
    *) cpkt_fail "unknown lifecycle option: $1" ;;
  esac
  shift 2
done
cpkt_check_group "$group"
case "$preset_explicit:$scope_explicit" in yes:yes|yes:no|no:yes|no:no) ;; *) cpkt_fail 'explicit selectors must be yes or no' ;; esac
case "$scope" in ''|selected|binary|release) ;; *) cpkt_fail "unknown SCOPE=$scope" ;; esac
cpkt_preset "$preset"
case "$action" in
  release|release-pipeline|release-matrix|release-final-matrix|prerelease|prerelease-hardening|test-all|deps-release|deps-cross|cross-build|cross-test|package-source|package-source-smoke|source-archive|verify-source-archive)
    [ "$group:$preset_explicit" = all:no ] || cpkt_fail "$action rejects GROUP/PRESET narrowing" ;;
esac
case "$action" in
  release|release-final-matrix|verify-release-archives|verify-release-privacy) expected_scope=release ;;
  release-pipeline|release-matrix|prerelease|prerelease-hardening|test-all|deps-release|deps-cross|cross-build|cross-test) expected_scope=binary ;;
  *) expected_scope= ;;
esac
if [ "$scope_explicit" = yes ] && [ -n "$expected_scope" ] && [ "$scope" != "$expected_scope" ]; then
  cpkt_fail "$action requires SCOPE=$expected_scope"
fi
if [ -n "$expected_scope" ]; then scope="$expected_scope"; fi
case "$action" in
  debug|test-debug|test-host|build-debug|build-host|clangd-surface|finalize-slice|valgrind|fuzz|fuzz-smoke|fuzz-long|examples|e2e-postgres|test-e2e)
    case "$preset" in debug|arm64-apple-darwin-debug) ;; *) cpkt_fail "$action requires native Debug PRESET" ;; esac ;;
esac
case "$action" in
  package|package-verify|package-checksums|test-install-tree)
    if [ "$group" = all ]; then
      [ "$preset_explicit" = no ] || cpkt_fail 'all packaging rejects PRESET narrowing'
      [ "$scope" != selected ] || cpkt_fail 'GROUP/SCOPE mismatch'
    else
      [ "$preset_explicit:$cpkt_configuration" = yes:Release ] || cpkt_fail 'selected packaging requires explicit Release PRESET'
      case "$scope" in ''|selected) ;; *) cpkt_fail 'GROUP/SCOPE mismatch' ;; esac
    fi ;;
  format|format-check) [ -z "$scope" ] || cpkt_fail 'formatting does not accept SCOPE' ;;
  deps) [ -n "$dependency" ] || cpkt_fail 'deps requires DEPENDENCY=<name>' ;;
  e2e-postgres|test-e2e|dev-*) [ "$cpkt_owner" = db ] || cpkt_fail 'database operations require cpktdb' ;;
  fuzz*) [ "$cpkt_owner" != db ] || cpkt_fail 'db fuzz is unsupported' ;;
  example-*|e2e-sus|e2e-cpktxscribe|cpktxscribe) [ "$cpkt_owner" = misc ] || cpkt_fail 'audio/speech operations require cpktmisc' ;;
esac
export GROUP="$group" PRESET="$preset" SCOPE="$scope"
cpkt_locked "$action" --group "$group" --preset "$preset" --preset-explicit "$preset_explicit" \
  --scope "$scope" --scope-explicit "$scope_explicit" --dependency "$dependency"

run_build() { bash "$cpkt_scripts/build.sh" "$@" --group "$group" --preset "$preset"; }
run_action() { bash "$cpkt_scripts/lifecycle.sh" "$1" --group "$group" --preset "$preset" --preset-explicit no; }

case "$action" in
  build|build-release|test|test-cross|cross-build|cross-test)
    mode=build
    case "$action" in test|test-cross|cross-test) mode=test ;; esac
    if [ "$group" != all ] || [ "$preset_explicit" = yes ]; then
      run_build "$mode"
    else
      while IFS= read -r target; do
        case "$target" in *-linux-*) bash "$cpkt_scripts/build.sh" "$mode" --group all --preset "$target-release" ;; esac
      done < <(cpkt_info targets)
    fi ;;
  build-debug|build-host) run_build build ;;
  debug|test-debug|test-host) run_build test ;;
  deps|deps-all|deps-debug)
    if [ "$action" = deps ]; then run_build deps --dependency "$dependency";
    elif [ "$action" = deps-all ]; then run_build deps;
    else run_build configure; fi ;;
  deps-release|deps-cross)
    while IFS= read -r target; do
      if [ "$action" = deps-release ] && [[ "$target" == *darwin ]]; then continue; fi
      if [ "$action" = deps-cross ] && [[ "$target" == x86_64-* ]]; then continue; fi
      bash "$cpkt_scripts/build.sh" configure --group all --preset "$target-release"
    done < <(cpkt_info targets) ;;
  examples)
    [ "$(cpkt_info examples)" = ON ] || cpkt_fail 'owning repository has no example tests'
    run_build test --regex example ;;
  clangd-surface)
    run_build build
    bash "$cpkt_scripts/verify-clangd-surface.sh" "$cpkt_root" "$cpkt_binary" --group "$group" ;;
  finalize-slice)
    run_action format
    run_action debug
    run_action clangd-surface
    run_action format-check ;;
  format|format-check) bash "$cpkt_scripts/format.sh" "$action" ;;
  clean|clean-dist) bash "$cpkt_scripts/clean.sh" "$action" --group "$group" ;;
  package|package-verify|package-checksums|test-install-tree|verify-release-archives|verify-release-privacy)
    bash "$cpkt_scripts/package.sh" "$action" --group "$group" --preset "$preset" --scope "$scope" ;;
  package-source) bash "$cpkt_scripts/package-source.sh" ;;
  package-source-smoke|source-archive|verify-source-archive)
    if [ "$action" != verify-source-archive ]; then bash "$cpkt_scripts/package-source.sh"; fi
    version=$(bash "$cpkt_scripts/release-version.sh" "$cpkt_root")
    bash "$cpkt_scripts/source-archive-verify.sh" "$cpkt_root/dist/$cpkt_provider-$version.tar.gz" "$version" ;;
  release|release-pipeline|release-matrix|release-final-matrix|prerelease|prerelease-hardening|test-all)
    bash "$cpkt_scripts/release.sh" "$action" ;;
  valgrind) bash "$cpkt_scripts/build.sh" memcheck --group "$group" --preset valgrind ;;
  fuzz) bash "$cpkt_scripts/fuzz.sh" standard ;;
  fuzz-smoke|fuzz-long) bash "$cpkt_scripts/fuzz.sh" "${action#fuzz-}" ;;
  lifecycle-version-contract) bash "$cpkt_scripts/version-contract.sh" check ;;
  deps-core) bash "$cpkt_scripts/core-dependency.sh" "$cpkt_target" ;;
  test-darwin-native|test-darwin-sdk) bash "$cpkt_scripts/darwin.sh" "$action" ;;
  test-github-actions-contracts) python3 "$cpkt_root/tests/github_actions_contract_test.py" ;;
  e2e-postgres|test-e2e)
    run_build build --target cpkt_postgres_integration_test
    bash "$cpkt_scripts/test-e2e.sh" "$cpkt_binary/cpkt_postgres_integration_test" ;;
  dev-*) bash "$cpkt_scripts/devenv.sh" "${action#dev-}" ;;
  *) bash "$cpkt_scripts/misc-workflow.sh" "$action" --preset "$preset" --preset-explicit "$preset_explicit" ;;
esac
