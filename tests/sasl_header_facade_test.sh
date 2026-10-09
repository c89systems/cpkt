#!/usr/bin/env bash
set -euo pipefail

repo_root=${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
headers=("$repo_root/include/cpkt/sasl.h" "$repo_root/include/cpkt/sasl_plugin.h")
include_dir="$repo_root/include"
cc=${CC:-cc}
mkdir -p "$repo_root/build"
work_root=$(mktemp -d "$repo_root/build/cpkt-sasl-header.XXXXXX")
trap 'rm -rf "$work_root"' EXIT

for forbidden in 'sasl/' 'stdint.h' 'stdbool.h' 'uint32_t' 'int32_t' 'long long' 'extern "C"' 'inline'; do
  if rg -F -- "$forbidden" "${headers[@]}" >/dev/null 2>&1; then
    printf 'SASL C89 facade header contains forbidden token: %s\n' "$forbidden" >&2
    exit 1
  fi
done

cat > "$work_root/sasl_header_c89.c" <<'EOF'
#include <cpkt/sasl_plugin.h>

int main(void) {
  cpkt_sasl *client;
  cpkt_sasl_md5_context hash;
  cpkt_sasl_security_properties security;
  cpkt_sasl_http_request request;
  const void *payload;
  int status;

  cpkt_sasl_md5_initialize(&hash);
  client = cpkt_sasl_client_new("imap", "mail.example.test", 0, 0, 0, 0,
      &status);
  if (client != 0) {
    client->get_security_properties(client, &security);
    client->get_http_request(client, &request);
    client->get_delegated_payload(client, &payload);
    client->close(client);
  }
  return 0;
}
EOF

"$cc" -std=c89 -Wall -Wextra -Wpedantic -Werror -I "$include_dir" \
  -c "$work_root/sasl_header_c89.c" -o "$work_root/sasl_header_c89.o"
