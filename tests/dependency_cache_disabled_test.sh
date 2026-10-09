#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  printf 'usage: dependency_cache_disabled_test.sh <source-dir> <external-root> <dependency-build-root>\n' >&2
  exit 2
fi

source_dir=$1
external_root=$2
dependency_build_root=$3
mkdir -p "$source_dir/build"
work_dir=$(mktemp -d "$source_dir/build/dependency-cache-disabled.XXXXXXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

configure_log="$work_dir/configure.log"

mkdir -p "$work_dir/source"
cat > "$work_dir/source/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.21)
project(disabled_caller_roots C)
set(CPKT_BUILD_DEPENDENCIES OFF)
set(CPKT_TARGET_ARCH x86_64)
set(CPKT_TARGET_OS linux)
set(CPKT_TARGET_LIBC gnu)
set(CPKT_DEPENDENCY_BUILD_TYPE Release)
include("${CPKT_REPO_ROOT}/cmake/CpktDependencies.cmake")
cpkt_add_openssl()
CMAKE
if env -u HOME -u XDG_CACHE_HOME -u CPKT_DEPENDENCY_CACHE cmake \
  -S "$work_dir/source" -B "$work_dir/binary" \
  -DCPKT_REPO_ROOT="$source_dir" \
  -DCPKT_EXTERNAL_ROOT="$external_root" \
  -DCPKT_DEPENDENCY_BUILD_ROOT="$dependency_build_root" >"$configure_log" 2>&1; then
  printf 'disabled real recipe accepted empty caller-owned roots\n' >&2; exit 1
fi

if ! grep -F "$external_root/openssl/install/lib/libcrypto.a" "$configure_log" >/dev/null 2>&1; then
  cat "$configure_log" >&2
  printf 'configure failure did not point at the caller-owned dependency root\n' >&2
  exit 1
fi

if grep -F 'repo-local external dependency root' "$configure_log" >/dev/null 2>&1; then
  cat "$configure_log" >&2
  printf 'caller-owned disabled dependency roots must not use lifecycle stale-root invalidation\n' >&2
  exit 1
fi
