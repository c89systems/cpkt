#!/usr/bin/env bash
set -euo pipefail
repo_root=${1:?repository root required}
exec python3 "$repo_root/tests/package_smoke_contract_test.py" Contracts.test_discovery_only_pinned_tools
