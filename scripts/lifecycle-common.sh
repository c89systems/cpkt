#!/usr/bin/env bash
# Shared path/argument handling; CMake remains the build graph authority.

cpkt_scripts=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cpkt_root=$(CDPATH= cd -- "$cpkt_scripts/.." && pwd)
cpkt_cmake=${CMAKE:-cmake}
cpkt_ctest=${CTEST:-ctest}

cpkt_fail() {
  printf 'cpkt: %s\n' "$*" >&2
  exit 2
}

cpkt_info() {
  "$cpkt_cmake" "-DCPKT_REPO_ROOT=$cpkt_root" "-DCPKT_INFO=$1" \
    -P "$cpkt_root/cmake/lifecycle-info.cmake"
}

cpkt_owner=$(cpkt_info owner)
case "$cpkt_owner" in core|db|misc) ;; *) cpkt_fail 'invalid repository owner' ;; esac
case "$cpkt_owner" in core) cpkt_provider=cpkt ;; db) cpkt_provider=cpktdb ;; misc) cpkt_provider=cpktmisc ;; esac

cpkt_check_group() {
  case "$1" in all|"$cpkt_owner") ;; *) cpkt_fail "GROUP=$1 is not owned by this repository ($cpkt_owner)" ;; esac
}

cpkt_owned_path() {
  local path=$1 parent
  case "$path" in "$cpkt_root/build/"*|"$cpkt_root/.cache/"*) ;; *) cpkt_fail "path is not owned generated state: $path" ;; esac
  case "$path/" in *'/../'*|*'/./'*) cpkt_fail 'generated path contains traversal' ;; esac
  parent=$path
  while [ "$parent" != "$cpkt_root" ]; do
    [ ! -L "$parent" ] || cpkt_fail "generated path has a symlink ancestor: $parent"
    parent=${parent%/*}
  done
}

cpkt_cache_value() {
  local key=$1 file=$2 line
  [ -f "$file" ] || return 1
  while IFS= read -r line; do
    case "$line" in
      "$key":*=*) printf '%s\n' "${line#*=}"; return ;;
    esac
  done < "$file"
  return 1
}

cpkt_preset() {
  local preset=$1 listed
  listed=$(cd "$cpkt_root" && "$cpkt_cmake" --list-presets=configure)
  [[ "$listed" == *\""$preset"\"* ]] || cpkt_fail "unknown configure preset: $preset"
  cpkt_configuration=Release
  case "$preset" in
    debug) cpkt_target=x86_64-linux-gnu; cpkt_configuration=Debug ;;
    release) cpkt_target=x86_64-linux-gnu ;;
    valgrind) cpkt_target=x86_64-linux-gnu; cpkt_configuration=Valgrind ;;
    fuzz|opcua-fuzz) cpkt_target=x86_64-linux-gnu; cpkt_configuration=Fuzz ;;
    arm64-apple-darwin-native) cpkt_target=arm64-apple-darwin ;;
    arm64-apple-darwin-debug) cpkt_target=arm64-apple-darwin; cpkt_configuration=Debug ;;
    *-release) cpkt_target=${preset%-release} ;;
    *) cpkt_fail "preset has no documented lifecycle profile: $preset" ;;
  esac
  cpkt_binary="$cpkt_root/build/$cpkt_target/$cpkt_owner/$cpkt_configuration"
  export CPKT_PRESET="$preset" CPKT_RESOLVED_TARGET="$cpkt_target"
}

cpkt_jobs() {
  local binary=$1 limit=8 jobs
  if [ "$(uname -s)" = Darwin ]; then limit=2; fi
  jobs=${CPKT_DEPENDENCY_BUILD_JOBS:-${CMAKE_BUILD_PARALLEL_LEVEL:-}}
  if [ -z "$jobs" ]; then
    jobs=$(cpkt_cache_value CPKT_DEPENDENCY_BUILD_JOBS "$binary/CMakeCache.txt") || jobs=$limit
  fi
  [[ "$jobs" =~ ^[1-9][0-9]*$ ]] || cpkt_fail 'job limit must be a positive integer'
  [ "$jobs" -le "$limit" ] || cpkt_fail "job limit exceeds configured host maximum $limit"
  printf '%s\n' "$jobs"
}

cpkt_locked() {
  if [ -z "${CPKT_OPERATION_FD:-}" ]; then
    exec bash "$cpkt_scripts/operation.sh" --root "$cpkt_root" --group "${GROUP:-all}" -- bash "$0" "$@"
  fi
  bash "$cpkt_scripts/operation.sh" --root "$cpkt_root" --group "${GROUP:-all}" --check
}
