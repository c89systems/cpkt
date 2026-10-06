#!/usr/bin/env bash
set -euo pipefail
scripts=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(CDPATH= cd -- "$scripts/.." && pwd)
python3 "$scripts/cpkt_lock.py" --root "$root" --group "${CPKT_OPERATION_SCOPE:-all}"
source "$scripts/mutation-paths.sh"
# Native directory selectors also name the graph when launched from its source
# directory (as CMake does during generation).
directory=$PWD
selected=no
for argument in "$@"; do
  if [ "$selected" = yes ]; then
    case "$argument" in /*) directory=$argument ;; *) directory="$directory/$argument" ;; esac
    selected=no
    continue
  fi
  case "$argument" in
    -C|--directory) selected=yes ;;
    -C?*) directory_argument=${argument#-C}
      case "$directory_argument" in /*) directory=$directory_argument ;; *) directory="$directory/$directory_argument" ;; esac ;;
    --directory=*) directory_argument=${argument#*=}
      case "$directory_argument" in /*) directory=$directory_argument ;; *) directory="$directory/$directory_argument" ;; esac ;;
  esac
done
cpkt_validate_mutation_path "$directory"
context=$directory
cache=
while [[ "$directory/" == "$root/"* ]]; do
  if [ -f "$directory/CMakeCache.txt" ]; then cache="$directory/CMakeCache.txt"; break; fi
  [ "$directory" != "$root" ] || break
  directory=${directory%/*}
done
value() {
  local key=$1 line
  [ -n "$cache" ] || return 1
  while IFS= read -r line; do
    case "$line" in "$key":*=*) printf '%s\n' "${line#*=}"; return ;; esac
  done < "$cache"
  return 1
}
native=$(value CPKT_NATIVE_MAKE_PROGRAM) || native=${CPKT_NATIVE_MAKE_PROGRAM:-}
[ -x "$native" ] && [ "$native" != "$scripts/native-build.sh" ] || {
  printf 'Configured native build tool is missing or recursive\n' >&2; exit 2;
}
# Ninja creates output parents before executing custom commands. Validate the
# explicit CMake-owned products before any native child or evidence removal.
if [ -n "$cache" ]; then
  outputs="${cache%/*}/cpkt-generated-outputs.txt"
  cpkt_validate_mutation_path "$outputs"
fi
# CMake's scratch project may query the tool before writing its own cache,
# while a previous parent consumer cache is already present. Identify that
# native CMake context, never a missing-inventory fallback for consumers.
probe=no
case "$context/" in
  "$root/"*/CMakeFiles/CMakeScratch/TryCompile-*/|"$root/"*/CMakeFiles/CMakeTmp/)
    if [ -f "$context/CMakeLists.txt" ]; then
      while IFS= read -r line; do
        case "$line" in project\(CMAKE_TRY_COMPILE\ *) probe=yes; break ;; esac
      done < "$context/CMakeLists.txt"
    fi ;;
esac
producer=$(value CPKT_DEPENDENCY_PRODUCER) || producer=OFF
project=$(value CMAKE_PROJECT_NAME) || project=
if [ "$project" = CMAKE_TRY_COMPILE ]; then probe=yes; fi
if [ -n "$cache" ] && [ -f "$outputs" ]; then
  # A zero-byte inventory is an explicit, valid CMake-generated empty set.
  while IFS= read -r output || [ -n "$output" ]; do
    cpkt_validate_mutation_path "$output"
  done < "$outputs"
elif [ -n "$cache" ] && [ -e "$outputs" ]; then
  printf 'Generated-output inventory must be a regular file: %s\n' "$outputs" >&2
  exit 2
elif [ "$producer" != ON ] && [ "$probe" != yes ]; then
  # Only an initial version query without a configured graph needs no list.
  if [ -n "$cache" ] || [ -f "$context/build.ninja" ] || [ -f "$context/cmake_install.cmake" ] ||
      [ "$#" != 1 ] || [ "$1" != --version ]; then
    printf 'Required generated-output inventory is missing for %s; prepare the consumer graph with scripts/build.sh configure --group <group> --preset <preset> (or rerun its authenticated CMake configure).\n' "$context" >&2
    exit 2
  fi
fi
removals=()
for argument in "$@"; do
  case "$argument" in clean|--clean)
    target=$(value CPKT_TARGET_ID) || target=
    group=$(value CPKT_GROUP) || group=
    if [ -n "$target" ] && [ -n "$group" ]; then
      evidence="$root/build/verification/$target/$group"
      cpkt_validate_mutation_path "$evidence"
      removals+=("$evidence/"*-development.json "$evidence/"*-built.json)
      if [ "$producer" = ON ]; then
        removals+=("$evidence/"component-*.json)
      fi
    fi ;;
  esac
done
for file in "${removals[@]}"; do cpkt_validate_mutation_path "$file"; done
if [ "${#removals[@]}" -gt 0 ]; then rm -f -- "${removals[@]}"; fi
exec "$native" "$@"
