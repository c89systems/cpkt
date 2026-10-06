#!/usr/bin/env bash
set -euo pipefail
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
mode=smoke
preset=${PRESET:-debug}
group=${GROUP:-all}
if [[ $group == all ]]; then group=core; fi
if [[ ${1:-} != --* && $# -gt 0 ]]; then mode=$1; shift; fi
while [[ $# -gt 0 ]]; do
  case "$1" in
    --group)
      [[ $# -ge 2 ]] || { printf 'missing --group value\n' >&2; exit 2; }
      [[ $group == all || $group == "$2" ]] || { printf 'conflicting GROUP selectors\n' >&2; exit 2; }
      group=$2; shift 2 ;;
    --preset)
      [[ $# -ge 2 ]] || { printf 'missing --preset value\n' >&2; exit 2; }
      preset=$2; shift 2 ;;
    *) printf 'unknown fuzz argument: %s\n' "$1" >&2; exit 2 ;;
  esac
done
[[ $preset == debug ]] || { printf 'fuzz requires native ordinary PRESET=debug\n' >&2; exit 2; }
export GROUP="$group"
case "$mode" in smoke|standard|long) ;; *) printf 'unknown fuzz mode: %s\n' "$mode" >&2; exit 2 ;; esac
if [[ $mode == long && ${CPKT_FUZZ_LONG_ENABLE:-0} != 1 ]]; then
  printf 'long fuzz requires CPKT_FUZZ_LONG_ENABLE=1\n' >&2
  exit 2
fi
case "$group" in
  db) printf 'AFL fuzz is unsupported for GROUP=db; no db fuzz target exists\n' >&2; exit 2 ;;
  core|misc|all) ;;
  *) printf 'unknown GROUP: %s\n' "$group" >&2; exit 2 ;;
esac
if [[ -z ${CPKT_OPERATION_FD:-} ]]; then
  exec bash "$script_dir/operation.sh" --group "$group" -- bash "$0" "$mode" --preset "$preset"
fi
bash "$script_dir/operation.sh" --group "$group" --check
cd "$repo_root"
if [[ $group == core || $group == all ]]; then
  bash "$script_dir/build.sh" build --group core --preset fuzz --target cpkt_lua_runtime_fuzz
  directory=$(bash "$script_dir/build.sh" path --group core --preset fuzz)
  bash "$script_dir/run-afl-fuzz.sh" "$mode" "$directory/cpkt_lua_runtime_fuzz" fuzz/seeds/lua
fi
