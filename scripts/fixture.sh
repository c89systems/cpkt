#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
scripts=$cpkt_scripts
original=("$@")
group=${GROUP:-all}
while [ "$#" -gt 0 ] && [ "$1" != -- ]; do
  [ "$#" -ge 2 ] || cpkt_fail 'fixture option requires a value'
  if [ "$1" = --group ]; then group=$2; fi
  shift 2
done
cpkt_check_group "$group"
export GROUP="$group"
set -- "${original[@]}"
cpkt_locked "$@"
arguments=$(mktemp "${TMPDIR:?owned temporary directory required}/fixture.XXXXXXXX")
trap 'rm -f -- "$arguments"' EXIT
python3 "$scripts/cpkt_fixture_identity.py" "$@" > "$arguments"
identity=()
scratch=
while IFS= read -r -d '' argument; do identity+=("$argument"); done < "$arguments"
for ((index=0; index<${#identity[@]}; index++)); do
  if [ "${identity[$index]}" = --output ]; then scratch=${identity[$((index+1))]}; break; fi
done
[ -n "$scratch" ] || { printf 'fixture scratch identity missing\n' >&2; exit 2; }
mkdir -p "$scratch"
bash "$scripts/helper.sh" "${identity[@]}"
