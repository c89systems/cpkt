#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
  printf 'usage: cpktxscribe_link_policy_test.sh <cpktxscribe>\n' >&2
  exit 2
fi

tool=$1

if ! command -v readelf >/dev/null 2>&1; then
  printf 'readelf is required to inspect cpktxscribe link policy\n' >&2
  exit 1
fi

if [ ! -x "$tool" ]; then
  printf 'missing executable cpktxscribe: %s\n' "$tool" >&2
  exit 1
fi

if ! readelf -l "$tool" | grep -F 'Requesting program interpreter:' >/dev/null 2>&1; then
  printf 'cpktxscribe is not a dynamically linked host executable\n' >&2
  readelf -l "$tool" >&2
  exit 1
fi

needed=$(readelf -d "$tool" | sed -n 's/.*Shared library: \[\(.*\)\].*/\1/p')

for forbidden in \
  'libcpktaudio.so' \
  'libcpktsus.so' \
  'libcurl.so' \
  'libcrypto.so' \
  'libssl.so' \
  'libwhisper.so' \
  'libggml' \
  'libstdc++.so' \
  'libgcc_s.so'
do
  if printf '%s\n' "$needed" | grep -F "$forbidden" >/dev/null 2>&1; then
    printf 'cpktxscribe has forbidden dynamic dependency matching %s\n' "$forbidden" >&2
    readelf -d "$tool" >&2
    exit 1
  fi
done

if ! printf '%s\n' "$needed" | grep -F 'libc.so' >/dev/null 2>&1; then
  printf 'cpktxscribe does not depend on the host C runtime\n' >&2
  readelf -d "$tool" >&2
  exit 1
fi
