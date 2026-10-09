#!/usr/bin/env bash
set -euo pipefail
repo=${1:?repository required}
source "$repo/scripts/require-host-bash.sh"
for case in '3 2 no' '4 0 no' '4 3 no' '4 4 yes' '4 44 yes' '5 0 yes' '6 1 yes' '04 04 yes' '0 99 no' '4.4 0 no' '4 -1 no' 'x 4 no' '4 4x no'; do
  read -r major minor expected <<< "$case"
  actual=no
  if cpkt_host_bash_supported "$major" "$minor"; then actual=yes; fi
  [ "$actual" = "$expected" ] || { printf 'Wrong Bash version result: %s\n' "$case" >&2; exit 1; }
done
if cpkt_host_bash_supported '' 4 || cpkt_host_bash_supported 4 ''; then exit 1; fi
count() { printf '%s\n' "$#"; }
arguments=()
[ "$(count "${arguments[@]}")" = 0 ]
arguments=('')
[ "$(count "${arguments[@]}")" = 1 ]
