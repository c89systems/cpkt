#!/usr/bin/env bash
set -euo pipefail

repo_root=$1
compiler=$2
krb5_lib=$3
openssl_lib=$4
work_root=$(mktemp -d "$repo_root/build/krb5-tls-module.XXXXXX")
trap 'rm -rf "$work_root"' EXIT
mkdir -p "$work_root/lib"
cp -a "$krb5_lib/." "$work_root/lib/"
cp -a "$openssl_lib"/libssl.so* "$openssl_lib"/libcrypto.so* "$work_root/lib/"
"$compiler" -std=c89 -Wall -Wextra -Werror \
  "$repo_root/tests/krb5_tls_module_smoke.c" -ldl -o "$work_root/smoke"
env -u LD_LIBRARY_PATH "$work_root/smoke" \
  "$work_root/lib/krb5/plugins/tls/k5tls.so" "$work_root/lib"
