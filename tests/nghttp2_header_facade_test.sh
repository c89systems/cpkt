#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 5 ]]; then
  printf 'usage: %s <repo-root> <c-compiler> <cxx-compiler> <sysroot> <facade-include-dir>\n' "$0" >&2
  exit 2
fi

repo_root=$1
compiler=$2
cxx_compiler=$3
sysroot=$4
facade_include_dir=$5
header="$facade_include_dir/cpkt/nghttp2.h"
mkdir -p "$repo_root/build"
work_root=$(mktemp -d "$repo_root/build/nghttp2-header-facade.XXXXXX")

cleanup() {
  cmake -E remove_directory "$work_root"
}
trap cleanup EXIT INT TERM

if [[ ! -f "$header" ]]; then
  printf 'generated nghttp2 C89 facade header is missing: %s\n' "$header" >&2
  exit 2
fi
if grep -Eq '^#include <nghttp2/' "$header"; then
  printf 'nghttp2 C89 facade header leaks an upstream header\n' >&2
  exit 1
fi

cat > "$work_root/nghttp2_header_c89.c" <<'EOF'
#include <cpkt/nghttp2.h>

int main(void) {
  cpkt_nghttp2_u64 rate;
  const cpkt_nghttp2_info *info;

  rate.high = 0UL;
  rate.low = 1UL;
  info = cpkt_nghttp2_version(0);
  return info != 0 && rate.low == 1UL ? 0 : 1;
}
EOF

compiler_args=("$compiler")
if [[ -n "$sysroot" ]]; then
  compiler_args+=("--sysroot=$sysroot")
fi
"${compiler_args[@]}" -std=c89 -Wall -Wextra -Wpedantic -pedantic-errors -Werror \
  -I "$facade_include_dir" \
  -c "$work_root/nghttp2_header_c89.c" -o "$work_root/nghttp2_header_c89.o"

cat > "$work_root/nghttp2_header_cxx.cpp" <<'EOF'
#include <cpkt/nghttp2.h>

int main() {
  const cpkt_nghttp2_info *info = cpkt_nghttp2_version(0);
  return info == 0;
}
EOF

cxx_compiler_args=("$cxx_compiler")
if [[ -n "$sysroot" ]]; then
  cxx_compiler_args+=("--sysroot=$sysroot")
fi
"${cxx_compiler_args[@]}" -std=c++11 -Wall -Wextra -Wpedantic -pedantic-errors -Werror \
  -I "$facade_include_dir" \
  -c "$work_root/nghttp2_header_cxx.cpp" -o "$work_root/nghttp2_header_cxx.o"
