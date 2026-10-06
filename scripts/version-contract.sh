#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
action=${1:-check}
export GROUP=all
cpkt_locked "$action"
reference=refs/tags/v99.99.99
record() { python3 "$cpkt_scripts/cpkt_reserved_tag.py" "$1" --root "$cpkt_root" "${@:2}"; }
git_ref() { git -C "$cpkt_root" "$@"; }
recover() {
  local oid expected log
  oid=$(git_ref show-ref --verify --hash "$reference" 2>/dev/null) || oid=
  log=$(git_ref rev-parse --git-path "logs/$reference")
  case "$log" in /*) ;; *) log="$cpkt_root/$log" ;; esac
  expected=$(record inspect --oid "$oid" --log "$log")
  if [ -n "$oid" ]; then
    [ "$(git_ref cat-file -t "$oid")" = commit ] || cpkt_fail 'reserved ref is not lightweight'
    git_ref update-ref -d "$reference" "$expected"
  fi
  record clear
}
recover
if [ "$action" = recover ]; then exit 0; fi
[ "$action" = check ] || cpkt_fail 'usage: version-contract.sh check|recover'
expected=0.0.0
tagged=no
while IFS= read -r tag; do
  [[ "$tag" =~ ^v[0-9]+[.][0-9]+[.][0-9]+$ ]] || continue
  [ "$(git_ref cat-file -t "refs/tags/$tag")" = commit ] || cpkt_fail 'exact version tag must be lightweight'
  expected=${tag#v}
  tagged=yes
done < <(git_ref tag --points-at HEAD --list 'v[0-9]*.[0-9]*.[0-9]*' --sort=version:refname)
assert_version() {
  [ "$(bash "$cpkt_scripts/release-version.sh" "$cpkt_root")" = "$1" ] || cpkt_fail 'private version precedence mismatch'
  [ "$(make -s -C "$cpkt_root" print-release-version)" = "$1" ] || cpkt_fail 'public Make version precedence mismatch'
}
assert_version "$expected"
if [ "$tagged" = no ]; then
  oid=$(git_ref rev-parse HEAD)
  nonce=$(od -An -N32 -tx1 /dev/urandom | tr -d ' \n')
  record prepare --oid "$oid" --nonce "$nonce"
  trap recover EXIT
  trap 'exit 129' HUP
  trap 'exit 130' INT
  trap 'exit 143' TERM
  zeros=${oid//?/0}
  git_ref update-ref --create-reflog -m "cpkt-reserved-tag:$nonce" "$reference" "$oid" "$zeros"
  log=$(git_ref rev-parse --git-path "logs/$reference")
  case "$log" in /*) ;; *) log="$cpkt_root/$log" ;; esac
  record created --oid "$oid" --log "$log"
  assert_version 99.99.99
  recover
  trap - EXIT HUP INT TERM
  assert_version 0.0.0
fi
printf 'release version contract passed; exact HEAD precedence and reserved ref ownership verified\n'
