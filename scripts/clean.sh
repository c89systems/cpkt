#!/usr/bin/env bash
set -euo pipefail
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mode=${GROUP:-all}
if [[ $# -gt 0 && $1 != --* ]]; then mode=$1; shift; fi
if [[ $# -gt 0 ]]; then
  [[ $# -eq 2 && $1 == --group ]] || { printf 'usage: clean.sh [core|all|dist] [--group core|all]\n' >&2; exit 2; }
  [[ $mode == all || $mode == dist || $mode == "$2" ]] || { printf 'conflicting GROUP selectors\n' >&2; exit 2; }
  if [[ $mode == dist ]]; then export GROUP="$2"; else mode=$2; fi
fi
case "$mode" in
  all|core) exec python3 "$script_dir/group-build.py" clean --group "$mode" ;;
  dist) exec python3 "$script_dir/group-build.py" clean --dist-only ;;
  *) printf 'usage: %s [all|core|dist]\n' "$0" >&2; exit 2 ;;
esac
