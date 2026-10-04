#!/usr/bin/env bash
set -euo pipefail
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fresh=()
if [[ ${1:-} == --fresh ]]; then fresh=(--fresh); shift; fi
if [[ ${1:-} == --* ]]; then
  exec python3 "$script_dir/group-build.py" configure "${fresh[@]}" "$@"
fi
preset=${1:-${PRESET:-debug}}
if [[ $# -gt 0 ]]; then shift; fi
exec python3 "$script_dir/group-build.py" configure --preset "$preset" "${fresh[@]}" "$@"
