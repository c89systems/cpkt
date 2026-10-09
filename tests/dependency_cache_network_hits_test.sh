#!/usr/bin/env bash
set -euo pipefail
repo=${1:?repository required}
python=${2:?host Python required}
scratch=${CPKT_CONFIGURED_BINARY_DIR:-"$repo/build"}
mkdir -p "$scratch"
work=$(mktemp -d "$scratch/cache-origin.XXXXXXXX")
printf 'cache fixture\n' > "$work/fixture.tar.gz"
server_pid=
cleanup() {
  if [ -n "$server_pid" ]; then
    kill "$server_pid" 2>/dev/null || :
    wait "$server_pid" 2>/dev/null || :
  fi
  rm -rf -- "$work"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
"$python" "$repo/tests/archive_origin.py" "$work" &
server_pid=$!
for ((attempt=0; attempt<100; attempt++)); do
  [ ! -s "$work/port" ] || break
  kill -0 "$server_pid" 2>/dev/null || exit 1
  sleep 0.05
done
[ -s "$work/port" ] || exit 1
cmake "-DCPKT_REPO=$repo" "-DCPKT_WORK=$work" \
  -P "$repo/tests/dependency_cache_network_hits_test.cmake"
