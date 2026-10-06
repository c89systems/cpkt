#!/usr/bin/env bash
set -euo pipefail
scripts=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(CDPATH= cd -- "$scripts/.." && pwd)
python3 "$scripts/cpkt_lock.py" --root "$root" --group "${CPKT_OPERATION_SCOPE:-all}"
directory=$PWD
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
source "$scripts/mutation-paths.sh"
# Ninja creates output parents before executing custom commands. Validate the
# explicit CMake-owned products before any native child or evidence removal.
if [ -n "$cache" ]; then
  outputs="${cache%/*}/cpkt-generated-outputs.txt"
  cpkt_validate_mutation_path "$outputs"
  if [ -f "$outputs" ]; then
    while IFS= read -r output || [ -n "$output" ]; do
      [ -n "$output" ] || continue
      cpkt_validate_mutation_path "$output"
    done < "$outputs"
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
      producer=$(value CPKT_DEPENDENCY_PRODUCER) || producer=OFF
      if [ "$producer" = ON ]; then
        removals+=("$evidence/"component-*.json)
      fi
    fi ;;
  esac
done
for file in "${removals[@]}"; do cpkt_validate_mutation_path "$file"; done
if [ "${#removals[@]}" -gt 0 ]; then rm -f -- "${removals[@]}"; fi
exec "$native" "$@"
