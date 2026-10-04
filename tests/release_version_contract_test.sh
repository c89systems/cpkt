#!/usr/bin/env bash
set -euo pipefail
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
exec python3 "$repo_root/scripts/cpkt_reserved_tag.py" check
