#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
  printf 'usage: %s <repo-root> <compiler> <sysroot> <openssl-include-dir>\n' "$0" >&2
  exit 2
fi

repo_root=$1
compiler=$2
sysroot=$3
openssl_include_dir=$4
header="$repo_root/include/cpkt/openssl.h"
mkdir -p "$repo_root/build"
work_root=$(mktemp -d "$repo_root/build/openssl-header-facade.XXXXXX")

cleanup() {
  cmake -E remove_directory "$work_root"
}
trap cleanup EXIT INT TERM

while IFS= read -r native_header; do
  include_line="#include <openssl/${native_header}>"
  if ! grep -Fqx "$include_line" "$header"; then
    printf 'OpenSSL C89 facade omits staged public header: %s\n' "$native_header" >&2
    exit 1
  fi
done < <(find "$openssl_include_dir/openssl" -maxdepth 1 -type f -name '*.h' -printf '%f\n' | sort)

cat > "$work_root/openssl_header_c89.c" <<'EOF'
#include <cpkt/openssl.h>

int main(void) {
  return OPENSSL_init_ssl(0, 0) == 1 ? 0 : 1;
}
EOF

compiler_args=("$compiler")
if [[ -n "$sysroot" ]]; then
  compiler_args+=("--sysroot=$sysroot")
fi
"${compiler_args[@]}" -std=c89 -Wall -Wextra -Wpedantic -pedantic-errors -Werror \
  -I "$repo_root/include" -isystem "$openssl_include_dir" \
  -c "$work_root/openssl_header_c89.c" -o "$work_root/openssl_header_c89.o"
