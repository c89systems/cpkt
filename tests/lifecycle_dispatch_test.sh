#!/usr/bin/env bash
set -euo pipefail
repo=${1:?repository required}
work=${2:?scratch required}/dispatch
rm -rf -- "$work"
mkdir -p "$work/scripts"
cp -- "$repo/Makefile" "$work/Makefile"
cp -- "$repo/scripts/lifecycle.sh" "$work/scripts/"
cat > "$work/scripts/lifecycle-common.sh" <<'EOF'
cpkt_scripts=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cpkt_owner=core
cpkt_fail() { printf '%s\n' "$*" >&2; exit 2; }
cpkt_check_group() { case "$1" in all|core) ;; *) cpkt_fail 'wrong owner';; esac; }
cpkt_default_preset() { echo debug; }
cpkt_preset() { cpkt_configuration=Debug; }
cpkt_info() {
  printf '%s\n' x86_64-linux-gnu x86_64-linux-musl aarch64-linux-gnu aarch64-linux-musl armhf-linux-gnu armhf-linux-musl arm64-apple-darwin
}
EOF
cat > "$work/scripts/build.sh" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$(dirname "$0")/../calls"
EOF
unset GROUP PRESET SCOPE MAKEFLAGS MFLAGS MAKELEVEL
expect() {
  local expected=$1
  shift
  rm -f -- "$work/calls"
  make --no-print-directory -C "$work" "$@"
  [ "$(cat "$work/calls")" = "$expected" ] || { cat "$work/calls" >&2; exit 1; }
}
native=debug
[ "$(uname -s)" != Darwin ] || native=arm64-apple-darwin-debug
expect "build --group all --preset $native" build
expect "test --group all --preset $native" test
expect 'test --group all --preset aarch64-linux-musl-release' test PRESET=aarch64-linux-musl-release
for action in build-release cross-build test-cross cross-test; do
  mode=build
  case "$action" in test-cross|cross-test) mode=test;; esac
  expected=
  for target in x86_64-linux-gnu x86_64-linux-musl aarch64-linux-gnu aarch64-linux-musl armhf-linux-gnu armhf-linux-musl; do
    expected+="${expected:+$'\n'}$mode --group all --preset $target-release"
  done
  expect "$expected" "$action"
done
rm -f -- "$work/calls"
if make --no-print-directory -C "$work" build GROUP=db; then exit 1; fi
[ ! -e "$work/calls" ]
