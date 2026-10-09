#!/usr/bin/env bash
set -euo pipefail

repo_root=${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
header="$repo_root/include/cpkt/gssapi.h"
include_dir="$repo_root/include"
cc=${CC:-cc}
mkdir -p "$repo_root/build"
work_root=$(mktemp -d "$repo_root/build/cpkt-gssapi-header.XXXXXX")
trap 'rm -rf "$work_root"' EXIT

for forbidden in 'gssapi/' 'stdint.h' 'stdbool.h' 'uint32_t' 'int32_t' 'long long' 'extern "C"' 'inline'; do
  if rg -F -- "$forbidden" "$header" >/dev/null 2>&1; then
    printf 'GSSAPI C89 facade header contains forbidden token: %s\n' "$forbidden" >&2
    exit 1
  fi
done

cat > "$work_root/gssapi_header_c89.c" <<'EOF'
#include <cpkt/gssapi.h>

int main(void) {
  cpkt_gss_oid_set *mechanisms;
  cpkt_gss_status minor;

  mechanisms = 0;
  if (!cpkt_gss_status_is_error(cpkt_gss_indicate_mechanisms(&minor, &mechanisms))) {
    cpkt_gss_release_oid_set(&minor, &mechanisms);
  }
  return 0;
}
EOF

"$cc" -std=c89 -Wall -Wextra -Wpedantic -Werror -I "$include_dir" \
  -c "$work_root/gssapi_header_c89.c" -o "$work_root/gssapi_header_c89.o"
