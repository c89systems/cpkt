#!/usr/bin/env bash
set -euo pipefail
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export CPKT_PACKAGE_ACTIVE=1
exec python3 "$script_dir/cpkt_packages.py" package "$@"
