#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
scripts=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
arguments=("$@")
while [ "$#" -gt 0 ] && [ "$1" != -- ]; do
  [ "$#" -ge 2 ] || { printf 'helper option requires a value: %s\n' "$1" >&2; exit 2; }
  shift 2
done
[ "${1:-}" = -- ] || { printf 'helper command delimiter is required\n' >&2; exit 2; }
shift
[ "$#" -gt 0 ] || { printf 'helper command is required\n' >&2; exit 2; }
if python3 "$scripts/cpkt_helper_proof.py" --check "${arguments[@]}"; then
  exit 0
else
  status=$?
  [ "$status" = 10 ] || exit "$status"
fi
"$@"
python3 "$scripts/cpkt_helper_proof.py" --publish "${arguments[@]}"
