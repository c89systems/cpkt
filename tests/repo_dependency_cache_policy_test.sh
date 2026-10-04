#!/usr/bin/env bash
set -euo pipefail

repo_root=$1
contract="$repo_root/cmake/CpktDependencyContract.cmake"
dependencies="$repo_root/cmake/CpktDependencies.cmake"

if grep -R -n -E 'CPKT_DEPENDENCY_SET_ID|CpktDependency(Set|Toolchain)Identity|cpkt_dependency_(set|toolchain)_cache_id|dependency_(set|toolchain)_identity|CPKT_CURL_BUILD_CACHE_ID|CPKT_TOOLCHAIN_IDENTITY' "$repo_root/CMakeLists.txt" "$repo_root/cmake" "$repo_root/skills"; then
  printf 'repo-local dependency cache identity pathing must not be reintroduced\n' >&2
  exit 1
fi

for required in \
  'set(_cpkt_default_external_root "${CMAKE_SOURCE_DIR}/.cache/deps/${CPKT_TARGET_ID}")' \
  'set(_cpkt_default_dependency_build_root "${CMAKE_SOURCE_DIR}/.cache/deps-build/${CPKT_TARGET_ID}")' \
  'cpkt_prepare_dependency_component' \
  'CPKT_DEPENDENCY_CONTRACT_ROOT' \
  'file(REMOVE_RECURSE "${_root}")' \
  'build inputs changed or absent' \
  'caller-owned roots' \
  'must not contain symlinks'; do
  if ! grep -F "$required" "$repo_root/CMakeLists.txt" "$contract" >/dev/null 2>&1; then
    printf 'component dependency cache policy is missing: %s\n' "$required" >&2
    exit 1
  fi
done

if grep -n -E 'add_dependencies\\([^)]*cpkt_deps\\)' "$repo_root/CMakeLists.txt"; then
  printf 'facades and test targets must not depend on the universal cpkt_deps target\n' >&2
  exit 1
fi

python3 "$repo_root/scripts/cpkt_inventory_cli.py" --root "$repo_root" --group all --closure >/dev/null
for required in 'cpkt_deps_${_component}' 'DEPENDS ${_component_targets}' 'cpkt_deps_all DEPENDS ${_all_dependency_targets}' 'ExternalProject_Add_Step(${_last_project} cpkt-receipt'; do
  if ! grep -F "$required" "$dependencies" >/dev/null; then
    printf 'inventory-owned complete producer policy is missing: %s\n' "$required" >&2
    exit 1
  fi
done

python3 "$repo_root/tests/package_isolation_build_test.py" Isolation.test_stable_inode_bounded_wait_and_owner_interruption
python3 "$repo_root/tests/package_recipe_graph_test.py" "$repo_root"
for lifecycle_script in build.sh test.sh package.sh; do
  if grep -F 'bash "$repo_root/scripts/clean.sh" all' "$repo_root/scripts/$lifecycle_script" >/dev/null 2>&1; then
    printf 'scripts/%s must not clean generated state during normal lifecycle work\n' "$lifecycle_script" >&2
    exit 1
  fi
done
