#!/usr/bin/env bash
set -euo pipefail
repo_root=${1:?repository root is required}
exec python3 "$repo_root/tests/lua_symbol_test_registration_fixture.py" "$repo_root"
