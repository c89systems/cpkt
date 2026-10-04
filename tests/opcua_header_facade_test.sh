#!/usr/bin/env bash
set -euo pipefail

repo_root=${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
header="$repo_root/include/cpkt/opcua.h"
include_dir="$repo_root/include"

cc=${CC:-cc}
cxx=${CXX:-c++}
work_root=$(mktemp -d "${TMPDIR:-/tmp}/cpkt-opcua-header.XXXXXX")
trap 'rm -rf "$work_root"' EXIT

python3 "$repo_root/tools/opcua/header_contract.py" "$header"

cat > "$work_root/opcua_header_c89.c" <<'EOF'
#include <cpkt/opcua.h>

int main(void) {
  cpkt_opcua_value value;
  cpkt_opcua_value_clear(&value);
  return value.type;
}
EOF

"$cc" -std=c89 -Wall -Wextra -Wpedantic -Werror -I "$include_dir" \
  -c "$work_root/opcua_header_c89.c" -o "$work_root/opcua_header_c89.o"

cat > "$work_root/opcua_header_cpp98.cpp" <<'EOF'
#include <cpkt/opcua.h>

int main() {
  cpkt_opcua_node_id node_id = cpkt_opcua_node_id_null();
  return node_id.identifier_type;
}
EOF

"$cxx" -std=c++98 -Wall -Wextra -Wpedantic -Werror -I "$include_dir" \
  -c "$work_root/opcua_header_cpp98.cpp" -o "$work_root/opcua_header_cpp98.o"
