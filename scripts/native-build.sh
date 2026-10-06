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
for argument in "$@"; do
  case "$argument" in clean|--clean)
    target=$(value CPKT_TARGET_ID) || target=
    group=$(value CPKT_GROUP) || group=
    if [ -n "$target" ] && [ -n "$group" ]; then
      for file in "$root/build/verification/$target/$group/"*-development.json \
          "$root/build/verification/$target/$group/"*-built.json; do rm -f -- "$file"; done
      producer=$(value CPKT_DEPENDENCY_PRODUCER) || producer=OFF
      if [ "$producer" = ON ]; then
        for file in "$root/build/verification/$target/$group/"component-*.json; do rm -f -- "$file"; done
      fi
    fi ;;
  esac
done
exec "$native" "$@"
