#!/usr/bin/env bash
set -euo pipefail
python=${1:?host Python required}
scratch=${2:?test scratch required}
shift 2
work=$(mktemp -d "$scratch/curl-http.XXXXXXXX")
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
"$python" "$(dirname -- "${BASH_SOURCE[0]}")/curl_async_dns_server.py" --port-file "$work/port" &
server_pid=$!
for ((attempt=0; attempt<100; attempt++)); do
  [ ! -s "$work/port" ] || break
  kill -0 "$server_pid" 2>/dev/null || exit 1
  sleep 0.05
done
[ -s "$work/port" ] || { printf 'HTTP fixture did not become ready\n' >&2; exit 1; }
port=$(cat "$work/port")
# The caller passes the selected runner and one compiled client as literal argv.
"$@" --multi "$port"
