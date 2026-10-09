#!/usr/bin/env bash
set -euo pipefail

repo_root=$1
fixture_root="$repo_root/build/repo-dependency-component-contract-test"
caller_root="$repo_root/build/repo-dependency-component-contract-caller-owned"
symlink_root="$repo_root/build/repo-dependency-component-contract-symlink"
symlink_target="$repo_root/build/repo-dependency-component-contract-symlink-target"

cleanup() {
  cmake -E remove_directory "$fixture_root" "$caller_root" "$symlink_root" \
    "$symlink_target"
}
trap cleanup EXIT INT TERM
cleanup

write_fixture() {
  local root=$1
  mkdir -p "$root/cmake"
  cat > "$root/cmake/CpktDependencies.cmake" <<'EOF'
function(cpkt_add_openssl)
endfunction()
function(cpkt_add_curl)
endfunction()
function(cpkt_add_sqlite)
endfunction()
EOF
  cat > "$root/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(repo_dependency_component_contract NONE)
set(CPKT_TARGET_ID component-cachetest-linux-gnu)
set(CPKT_TARGET_ARCH component-cachetest)
set(CPKT_TARGET_OS Linux)
set(CPKT_TARGET_LIBC gnu)
set(CPKT_BUILD_DEPENDENCIES "${CPKT_BUILD_DEPENDENCIES}" CACHE BOOL "")
set(CPKT_EXTERNAL_ROOT "${CPKT_EXTERNAL_ROOT}" CACHE PATH "")
set(CPKT_DEPENDENCY_BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}" CACHE PATH "")
if(NOT CPKT_EXTERNAL_ROOT)
  set(CPKT_EXTERNAL_ROOT "${CMAKE_SOURCE_DIR}/.cache/deps/${CPKT_TARGET_ID}")
endif()
if(NOT CPKT_DEPENDENCY_BUILD_ROOT)
  set(CPKT_DEPENDENCY_BUILD_ROOT "${CMAKE_SOURCE_DIR}/.cache/deps-build/${CPKT_TARGET_ID}")
endif()
if(NOT DEFINED CPKT_EXTERNAL_ROOT_LIFECYCLE_OWNED)
  set(CPKT_EXTERNAL_ROOT_LIFECYCLE_OWNED ON)
endif()
if(NOT DEFINED CPKT_DEPENDENCY_BUILD_ROOT_LIFECYCLE_OWNED)
  set(CPKT_DEPENDENCY_BUILD_ROOT_LIFECYCLE_OWNED ON)
endif()
set(CPKT_DEPENDENCY_CONTRACT_ROOT "${CMAKE_SOURCE_DIR}/.cache/dependency-contracts")
include("${CPKT_CONTRACT_FILE}")
cpkt_prepare_dependency_component(
  NAME openssl
  BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/openssl"
  INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/openssl/install"
  VARIABLES CPKT_OPENSSL_VERSION
  RECIPE_FUNCTIONS cpkt_add_openssl)
cpkt_prepare_dependency_component(
  NAME curl
  BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/curl"
  INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/curl/install"
  VARIABLES CPKT_CURL_VERSION
  DEPENDS openssl
  RECIPE_FUNCTIONS cpkt_add_curl)
cpkt_prepare_dependency_component(
  NAME sqlite
  BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/sqlite"
  INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/sqlite/install"
  VARIABLES CPKT_SQLITE_VERSION
  RECIPE_FUNCTIONS cpkt_add_sqlite)
EOF
}

configure_fixture() {
  local root=$1
  shift
  cmake -S "$root" -B "$root/build" \
    -DCPKT_CONTRACT_FILE="$repo_root/cmake/CpktDependencyContract.cmake" "$@"
}

write_fixture "$fixture_root"
target_id=component-cachetest-linux-gnu
external_root="$fixture_root/.cache/deps/$target_id"
build_root="$fixture_root/.cache/deps-build/$target_id"
for component in openssl curl sqlite; do
  mkdir -p "$external_root/$component/install" "$build_root/$component"
  : > "$external_root/$component/install/sentinel"
  : > "$build_root/$component/sentinel"
done

first_output=$(configure_fixture "$fixture_root" -DCPKT_BUILD_DEPENDENCIES=ON -DCPKT_OPENSSL_VERSION=one -DCPKT_CURL_VERSION=one -DCPKT_SQLITE_VERSION=one 2>&1)
for component in openssl curl sqlite; do
  test ! -e "$external_root/$component/install/sentinel"
  test ! -e "$build_root/$component/sentinel"
  test -f "$fixture_root/.cache/dependency-contracts/$target_id/$component.txt"
  mkdir -p "$external_root/$component/install" "$build_root/$component"
  : > "$external_root/$component/install/sentinel"
  : > "$build_root/$component/sentinel"
done
case "$first_output" in
  *"Refreshed dependency openssl"*) ;;
  *) printf 'unverified existing state was not invalidated\n%s\n' "$first_output" >&2; exit 1 ;;
esac

sqlite_output=$(configure_fixture "$fixture_root" -DCPKT_BUILD_DEPENDENCIES=ON -DCPKT_OPENSSL_VERSION=one -DCPKT_CURL_VERSION=one -DCPKT_SQLITE_VERSION=two 2>&1)
case "$sqlite_output" in
  *"Refreshed dependency sqlite"*) ;;
  *) printf 'sqlite-only change did not refresh sqlite\n%s\n' "$sqlite_output" >&2; exit 1 ;;
esac
test ! -e "$external_root/sqlite/install/sentinel"
test ! -e "$build_root/sqlite/sentinel"
for component in openssl curl; do
  test -e "$external_root/$component/install/sentinel"
  test -e "$build_root/$component/sentinel"
done

mkdir -p "$external_root/sqlite/install" "$build_root/sqlite"
: > "$external_root/sqlite/install/sentinel"
: > "$build_root/sqlite/sentinel"
openssl_output=$(configure_fixture "$fixture_root" -DCPKT_BUILD_DEPENDENCIES=ON -DCPKT_OPENSSL_VERSION=two -DCPKT_CURL_VERSION=one -DCPKT_SQLITE_VERSION=two 2>&1)
for component in openssl curl; do
  case "$openssl_output" in
    *"Refreshed dependency $component"*) ;;
    *) printf 'OpenSSL change did not refresh dependent %s\n%s\n' "$component" "$openssl_output" >&2; exit 1 ;;
  esac
  test ! -e "$external_root/$component/install/sentinel"
  test ! -e "$build_root/$component/sentinel"
done
test -e "$external_root/sqlite/install/sentinel"
test -e "$build_root/sqlite/sentinel"

set +e
disabled_output=$(configure_fixture "$fixture_root" -DCPKT_BUILD_DEPENDENCIES=OFF -DCPKT_OPENSSL_VERSION=two -DCPKT_CURL_VERSION=one -DCPKT_SQLITE_VERSION=three 2>&1)
disabled_status=$?
set -e
if [ "$disabled_status" -eq 0 ]; then
  printf 'disabled dependency rebuild accepted stale sqlite state\n' >&2
  exit 1
fi
case "$disabled_output" in
  *"dependency component sqlite"*"build inputs"*"changed or absent"*) ;;
  *) printf 'disabled rebuild did not identify stale sqlite\n%s\n' "$disabled_output" >&2; exit 1 ;;
esac

write_fixture "$caller_root"
caller_external="$caller_root/caller-owned-external"
caller_build="$caller_root/.cache/deps-build/$target_id"
mkdir -p "$caller_external/sqlite/install" "$caller_build/sqlite"
: > "$caller_external/sqlite/install/sentinel"
: > "$caller_build/sqlite/sentinel"
set +e
caller_output=$(configure_fixture "$caller_root" -DCPKT_BUILD_DEPENDENCIES=ON -DCPKT_EXTERNAL_ROOT="$caller_external" -DCPKT_DEPENDENCY_BUILD_ROOT="$caller_build" -DCPKT_EXTERNAL_ROOT_LIFECYCLE_OWNED=OFF -DCPKT_OPENSSL_VERSION=one -DCPKT_CURL_VERSION=one -DCPKT_SQLITE_VERSION=two 2>&1)
caller_status=$?
set -e
if [ "$caller_status" -eq 0 ]; then
  printf 'caller-owned dependency roots accepted lifecycle deletion\n' >&2
  exit 1
fi
case "$caller_output" in
  *"caller-owned roots"*) ;;
  *) printf 'caller-owned root failure was not actionable\n%s\n' "$caller_output" >&2; exit 1 ;;
esac
test -e "$caller_external/sqlite/install/sentinel"
test -e "$caller_build/sqlite/sentinel"

write_fixture "$symlink_root"
mkdir -p "$symlink_target/cache/deps/$target_id/openssl/install" \
  "$symlink_target/cache/deps-build/$target_id/openssl"
: > "$symlink_target/cache/deps/$target_id/openssl/install/sentinel"
: > "$symlink_target/cache/deps-build/$target_id/openssl/sentinel"
cmake -E create_symlink "$symlink_target/cache" "$symlink_root/.cache"
set +e
symlink_output=$(configure_fixture "$symlink_root" -DCPKT_BUILD_DEPENDENCIES=ON \
  -DCPKT_OPENSSL_VERSION=two -DCPKT_CURL_VERSION=one \
  -DCPKT_SQLITE_VERSION=one 2>&1)
symlink_status=$?
set -e
if [ "$symlink_status" -eq 0 ]; then
  printf 'symlinked lifecycle dependency ancestor accepted recursive deletion\n' >&2
  exit 1
fi
case "$symlink_output" in
  *"symlink ancestor"*"$symlink_root/.cache"*) ;;
  *) printf 'symlinked ancestor failure was not actionable\n%s\n' "$symlink_output" >&2; exit 1 ;;
esac
test -e "$symlink_target/cache/deps/$target_id/openssl/install/sentinel"
test -e "$symlink_target/cache/deps-build/$target_id/openssl/sentinel"
