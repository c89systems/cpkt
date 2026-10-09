#!/usr/bin/env bash
set -euo pipefail
repo=${1:?repository required}
work=${2:?scratch required}/dispatch
rm -rf -- "$work"
mkdir -p "$work/scripts"
mkdir -p "$work/implicit"
printf 'int fixture(void) { return 0; }\n' > "$work/implicit/fixture.c"
printf 'all: fixture.o\n' > "$work/implicit/Makefile"
cp -- "$repo/Makefile" "$work/Makefile"
cp -- "$repo/scripts/lifecycle.sh" "$work/scripts/"
cp -- "$repo/scripts/package.sh" "$work/scripts/"
printf 'printf "0.0.0\\n"\n' > "$work/scripts/release-version.sh"
cat > "$work/scripts/lifecycle-common.sh" <<'EOF'
cpkt_scripts=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cpkt_root=${cpkt_scripts%/scripts}
cpkt_owner=core
cpkt_cmake=cpkt_mock_cmake
cpkt_mock_cmake() { printf '%s\n' "$*" >> "$cpkt_root/calls"; }
cpkt_owned_path() { :; }
cpkt_fail() { printf '%s\n' "$*" >&2; exit 2; }
cpkt_check_group() { case "$1" in all|core) ;; *) cpkt_fail 'wrong owner';; esac; }
cpkt_default_preset() { echo debug; }
cpkt_preset() {
  cpkt_configuration=Debug
  case "$1" in *-release|*-native) cpkt_configuration=Release;; esac
  cpkt_target=${1%-release}
  cpkt_target=${cpkt_target%-native}
  cpkt_binary="$cpkt_root/build/$1"
}
cpkt_info() {
  printf '%s\n' x86_64-linux-gnu x86_64-linux-musl aarch64-linux-gnu aarch64-linux-musl armhf-linux-gnu armhf-linux-musl arm64-apple-darwin
}
EOF
cat > "$work/scripts/build.sh" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$(dirname "$0")/../calls"
# Upstream dependencies may use Make's native implicit C compilation rules.
make --no-print-directory -C "$(dirname "$0")/../implicit" fixture.o >/dev/null
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
for preset in arm64-apple-darwin-native arm64-apple-darwin-release aarch64-linux-musl-release; do
  package_build="--build $work/build/$preset --target package-bundle"
  expect "test --group core --preset $preset"$'\n'"$package_build" \
    package GROUP=core PRESET="$preset" SCOPE=selected
  rm -f -- "$work/calls"
  bash "$work/scripts/package.sh" package-stage --group core --preset "$preset" --scope selected
  [ "$(cat "$work/calls")" = "$package_build" ] || { cat "$work/calls" >&2; exit 1; }
done
rm -f -- "$work/calls"
if make --no-print-directory -C "$work" build GROUP=db; then exit 1; fi
[ ! -e "$work/calls" ]
