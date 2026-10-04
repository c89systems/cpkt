#!/usr/bin/env bash
set -euo pipefail
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$script_dir/cpkt_verify_manifest.py" "$@"
