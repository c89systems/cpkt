#!/usr/bin/env bash
set -euo pipefail
repo=${1:?repository required}
scratch=${CPKT_CONFIGURED_BINARY_DIR:-"$repo/build/lifecycle-tests"}
mkdir -p "$scratch"
exec cmake "-DCPKT_ROOT=$repo" "-DCPKT_SCRATCH=$scratch" \
  -P "$repo/tests/dependency_inputs_test.cmake"
