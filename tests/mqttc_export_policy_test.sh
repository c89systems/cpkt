#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 6 ]]; then
  printf 'usage: %s <shared-library> <system-name> <nm> <allowlist> <compiler> <sysroot>\n' "$0" >&2
  exit 2
fi

library=$1
system_name=$2
symbol_tool=$3
allowlist=$4
compiler=$5
sysroot=$6

if [[ ! -f "$library" || ! -f "$allowlist" ]]; then
  printf 'MQTT-C facade export-policy inputs are missing\n' >&2
  exit 2
fi

if [[ "$system_name" == Darwin ]]; then
  symbols=$("$symbol_tool" -gU "$library" | awk '{ print $NF }')
else
  symbols=$("$symbol_tool" -D --defined-only "$library" | awk '{ print $NF }')
fi
expected=$(awk '!/^[[:space:]]*(#|$)/ { print }' "$allowlist" | sort -u)
actual=$(printf '%s\n' "$symbols" | sed '/^_\(init\|fini\)$/d' | sort -u)
if [[ "$actual" != "$expected" ]]; then
  printf 'libcpkt_mqttc dynamic export allowlist mismatch\nexpected:\n%s\nactual:\n%s\n' \
    "$expected" "$actual" >&2
  exit 1
fi

repo_root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$repo_root/build"
work_root=$(mktemp -d "$repo_root/build/mqttc-export-policy.XXXXXX")
cleanup() {
  cmake -E remove_directory "$work_root"
}
trap cleanup EXIT INT TERM

printf '%s\n' \
  'extern unsigned long cpkt_mqttc_private_layout_probe(void);' \
  'int main(void) { return (int)cpkt_mqttc_private_layout_probe(); }' \
  > "$work_root/private-sentinel.c"

compiler_args=("$compiler" -std=c89 -Wall -Wextra -Wpedantic -pedantic-errors -Werror)
if [[ -n "$sysroot" ]]; then
  compiler_args+=("--sysroot=$sysroot")
fi
if sentinel_output=$("${compiler_args[@]}" "$work_root/private-sentinel.c" \
    -L "$(dirname "$library")" -lcpkt_mqttc \
    -o "$work_root/private-sentinel" 2>&1); then
  printf 'private MQTT-C facade sentinel linked unexpectedly\n' >&2
  exit 1
fi
if [[ "$sentinel_output" != *cpkt_mqttc_private_layout_probe* ]]; then
  printf 'private-sentinel link failed for an unrelated reason:\n%s\n' \
    "$sentinel_output" >&2
  exit 1
fi
