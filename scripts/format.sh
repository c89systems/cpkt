#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
case "${1:-}" in
  format) options=(-i) ;;
  format-check) options=(--dry-run --Werror) ;;
  *) cpkt_fail 'usage: format.sh format|format-check' ;;
esac
files=()
for directory in include src tests examples fuzz tools; do
  [ -d "$cpkt_root/$directory" ] || continue
  while IFS= read -r -d '' file; do files+=("$file"); done < <(
    find "$cpkt_root/$directory" -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \
      -o -name '*.cc' -o -name '*.cxx' -o -name '*.hpp' \) -print0
  )
done
[ "${#files[@]}" -gt 0 ] || exit 0
clang-format "${options[@]}" "${files[@]}"
