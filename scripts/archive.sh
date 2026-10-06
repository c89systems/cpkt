#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 2 ] || { printf 'usage: archive.sh <prefix> <archive>\n' >&2; exit 2; }
prefix=$1
archive=$2
tar_tool=${CPKT_GNU_TAR:-}
if [ -z "$tar_tool" ]; then
  for candidate in gtar tar; do
    if command -v "$candidate" >/dev/null && "$candidate" --version | grep -q 'GNU tar'; then
      tar_tool=$(command -v "$candidate")
      break
    fi
  done
fi
[ -n "$tar_tool" ] && "$tar_tool" --version | grep -q 'GNU tar' || { printf 'GNU tar is required for deterministic SDK archives\n' >&2; exit 2; }
mkdir -p "$(dirname -- "$archive")"
temporary="$archive.tmp"
trap 'rm -f -- "$temporary"' EXIT
"$tar_tool" --create --format=gnu --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
  -C "$(dirname -- "$prefix")" "$(basename -- "$prefix")" | gzip -n -6 > "$temporary"
mv -- "$temporary" "$archive"
