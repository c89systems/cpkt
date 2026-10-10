#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
# Shared path/argument handling; CMake remains the build graph authority.

cpkt_scripts=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cpkt_root=$(CDPATH= cd -- "$cpkt_scripts/.." && pwd)
cpkt_cmake=${CMAKE:-cmake}
cpkt_ctest=${CTEST:-ctest}

cpkt_default_preset() {
  if [ "$(uname -s)" = Darwin ]; then printf 'arm64-apple-darwin-debug\n'; else printf 'debug\n'; fi
}

cpkt_fail() {
  printf 'cpkt: %s\n' "$*" >&2
  exit 2
}

cpkt_info() {
  "$cpkt_cmake" "-DCPKT_REPO_ROOT=$cpkt_root" "-DCPKT_INFO=$1" \
    -P "$cpkt_root/cmake/lifecycle-info.cmake"
}

cpkt_owner=$(cpkt_info owner)
[ "$cpkt_owner" = core ] || cpkt_fail 'this repository owns the core SDK'
cpkt_provider=cpkt

cpkt_check_group() {
  case "$1" in all|"$cpkt_owner") ;; *) cpkt_fail "GROUP=$1 is not owned by this repository ($cpkt_owner)" ;; esac
}

source "$cpkt_scripts/mutation-paths.sh"

cpkt_owned_path() {
  local path=$1
  case "$path" in "$cpkt_root/build"|"$cpkt_root/build/"*|"$cpkt_root/.cache/"*|"$cpkt_root/dist"|"$cpkt_root/dist/"*) ;; *) cpkt_fail "path is not owned generated state: $path" ;; esac
  case "$path/" in *'/../'*|*'/./'*) cpkt_fail 'generated path contains traversal' ;; esac
  cpkt_validate_mutation_path "$path"
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

# Generated state belongs to this checkout; only the global cpkt cache is shared.
cpkt_validate_cache_location() {
  local path=$1 workspace parent owner global
  cpkt_validate_mutation_path "$path" || return $?
  workspace=$(git -C "$cpkt_root" rev-parse --show-toplevel 2>/dev/null) || workspace=$cpkt_root
  parent=$path
  while [ ! -d "$parent" ] && [ "$parent" != / ]; do
    parent=${parent%/*}; [ -n "$parent" ] || parent=/
  done
  owner=$(git -C "$parent" rev-parse --show-toplevel 2>/dev/null) || owner=
  if [ -n "$owner" ] && [ "$owner" != "$workspace" ]; then
    cpkt_fail "cache belongs to another checkout: $path"
  fi
  global=${XDG_CACHE_HOME:-${HOME:?HOME is required}/.cache}/cpkt
  case "$path" in
    "$workspace/build"|"$workspace/build/"*|"$workspace/.cache"|"$workspace/.cache/"*|"$global"|"$global/"*) ;;
    *) cpkt_fail "cache must be checkout-owned generated state or the global cpkt cache: $path" ;;
  esac
}

cpkt_check_cache_overrides() {
  local name value
  for name in CPKT_TOOLCHAIN_CACHE CPKT_DEPENDENCY_CACHE; do
    value=${!name:-}
    [ -z "$value" ] || cpkt_validate_cache_location "$value"
  done
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
    fuzz) cpkt_target=x86_64-linux-gnu; cpkt_configuration=Fuzz ;;
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
