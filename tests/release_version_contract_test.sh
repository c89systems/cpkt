#!/usr/bin/env bash
set -euo pipefail
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
exec bash "$repo_root/scripts/version-contract.sh" check
