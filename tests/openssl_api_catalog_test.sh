#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 7 ]]; then
  printf 'usage: %s <repo-root> <libcrypto.num> <libssl.num> <libcrypto> <libssl> <elf|macho> <symbol-tool>\n' "$0" >&2
  exit 2
fi

repo_root=$1
crypto_num=$2
ssl_num=$3
crypto_library=$4
ssl_library=$5
symbol_format=$6
symbol_tool=$7
mkdir -p "$repo_root/build"
work_root=$(mktemp -d "$repo_root/build/openssl-api-catalog.XXXXXX")

cleanup() {
  cmake -E remove_directory "$work_root"
}
trap cleanup EXIT INT TERM

for path in "$crypto_num" "$ssl_num" "$crypto_library" "$ssl_library"; do
  if [[ ! -f "$path" ]]; then
    printf 'OpenSSL catalog input is missing: %s\n' "$path" >&2
    exit 2
  fi
done

awk '$4 ~ /(^|,)EXIST::FUNCTION/ { print $1 }' \
  "$crypto_num" "$ssl_num" | sort -u > "$work_root/declared-functions"

case "$symbol_format" in
  elf)
    "$symbol_tool" --dyn-syms --wide "$crypto_library" "$ssl_library" |
      awk '$4 == "FUNC" && ($5 == "GLOBAL" || $5 == "WEAK") && $7 != "UND" {
        name = $8
        sub(/@.*/, "", name)
        print name
      }' | sort -u > "$work_root/dynamic-functions"
    ;;
  macho)
    "$symbol_tool" -gU "$crypto_library" "$ssl_library" |
      awk '$2 ~ /^[Tt]$/ { print $3 }' | sort -u > "$work_root/dynamic-functions"
    ;;
  *)
    printf 'unsupported OpenSSL symbol format: %s\n' "$symbol_format" >&2
    exit 2
    ;;
esac

comm -12 "$work_root/declared-functions" "$work_root/dynamic-functions" \
  > "$work_root/effective-functions"
comm -13 "$work_root/declared-functions" "$work_root/dynamic-functions" \
  > "$work_root/extra-functions"

while IFS= read -r symbol; do
  [[ -n "$symbol" ]] || continue
  case "$symbol" in
    ERR_load_CRYPTO_strings|OCSP_crlID_new|OPENSSL_fork_prepare|OPENSSL_fork_parent|OPENSSL_fork_child)
      ;;
    *)
      printf 'unclassified OpenSSL dynamic function is not in the public manifest: %s\n' "$symbol" >&2
      exit 1
      ;;
  esac
done < "$work_root/extra-functions"

effective_count=$(wc -l < "$work_root/effective-functions")
declared_count=$(wc -l < "$work_root/declared-functions")
dynamic_count=$(wc -l < "$work_root/dynamic-functions")
if [[ "$effective_count" -eq 0 ]]; then
  printf 'OpenSSL effective public function inventory is empty\n' >&2
  exit 1
fi

printf 'OpenSSL public function inventory: declared=%s effective=%s dynamic=%s\n' \
  "$declared_count" "$effective_count" "$dynamic_count"
