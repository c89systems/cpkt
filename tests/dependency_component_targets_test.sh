#!/usr/bin/env bash
set -euo pipefail
repo_root=${1:?repo root is required}
# Producer/consumer cutover: inspect the real component recipes independently,
# rather than configuring a legacy monolithic consumer with producer stamps.
python3 "$repo_root/tests/package_recipe_graph_test.py" "$repo_root"
