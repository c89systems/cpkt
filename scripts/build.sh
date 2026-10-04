#!/usr/bin/env bash
set -euo pipefail
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
default_mode=release
if [[ ${GROUP:-all} != all ]]; then default_mode=debug; fi
mode=${1:-${PRESET:-$default_mode}}
if [[ $mode == --* && $mode != --matrix ]]; then
  exec python3 "$script_dir/group-build.py" build "$@"
fi
if [[ $# -gt 0 ]]; then shift; fi
case "$mode" in
  host) mode=debug ;;
  all|release)
    if [[ ${GROUP:-all} != all || -n ${PRESET:-} || $# -gt 0 ]]; then
      [[ $mode != all ]] || { printf 'all matrix rejects narrowing selectors\n' >&2; exit 2; }
      exec python3 "$script_dir/group-build.py" build --preset "${PRESET:-release}" "$@"
    fi
    exec python3 "$script_dir/cpkt_operation.py" --group all -- \
      bash "$script_dir/build.sh" --matrix "$@" ;;
  --matrix)
    [[ ${GROUP:-all} == all && -z ${PRESET:-} && $# -eq 0 ]] || { printf 'matrix rejects narrowing selectors\n' >&2; exit 2; }
    for preset in x86_64-linux-gnu-release x86_64-linux-musl-release aarch64-linux-gnu-release aarch64-linux-musl-release armhf-linux-gnu-release armhf-linux-musl-release; do
      python3 "$script_dir/group-build.py" build --preset "$preset" "$@"
    done
    exit ;;
esac
exec python3 "$script_dir/group-build.py" build --preset "$mode" "$@"
