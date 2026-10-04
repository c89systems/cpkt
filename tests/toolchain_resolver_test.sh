#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  printf 'usage: toolchain_resolver_test.sh <source-dir> [resolver-script]\n' >&2
  exit 2
fi

source_dir=$1
cache=$(mktemp -d)
failure_cache=$(mktemp -d)
fake_bin=$(mktemp -d)
trap 'rm -rf "$cache" "$failure_cache" "$fake_bin"' EXIT HUP INT TERM
bootlin="${2:-$source_dir/scripts/cpkt-toolchains.sh}"
fail() { printf 'toolchain resolver test: %s\n' "$*" >&2; exit 1; }
require_line() { grep -Fxq "$1" <<<"$2" || fail "missing output: $1"; }
require_text() { [[ "$2" == *"$1"* ]] || fail "missing output: $1"; }
make_executable() { printf '%b\n' "$2" > "$1"; chmod +x "$1"; }

grep -Fq 'with_cache_lock "$(cache_root)/locks/bootlin-$name.lock" install_bootlin_locked "$target"' "$bootlin" ||
  fail 'Bootlin root publication is not serialized by collection lock'
grep -Fq 'if bootlin_ready "$root" "$prefix" "$root/$sysroot_rel"; then return; fi' "$bootlin" ||
  fail 'Bootlin root readiness is not rechecked after acquiring the collection lock'

bootlin_name=x86-64--glibc--stable-2026.08-1
bootlin_root="$cache/roots/$bootlin_name"
bootlin_sysroot="$bootlin_root/x86_64-buildroot-linux-gnu/sysroot"
mkdir -p "$bootlin_root/bin" "$bootlin_sysroot/usr/include" "$bootlin_sysroot/usr/lib" "$bootlin_root/runtime"
: > "$bootlin_sysroot/usr/include/stdio.h"; : > "$bootlin_sysroot/usr/lib/libc.so"
: > "$bootlin_root/runtime/libstdc++.a"; : > "$bootlin_root/runtime/libgcc.a"
for tool in gcc ld ar ranlib strip nm objcopy objdump addr2line gdb readelf; do make_executable "$bootlin_root/bin/x86_64-linux-$tool" '#!/bin/sh\nexit 0'; done
make_executable "$bootlin_root/bin/x86_64-linux-g++" "#!/bin/sh\ncase \"\$1\" in\n  -print-file-name=libstdc++.a) printf '%s\\n' '$bootlin_root/runtime/libstdc++.a' ;;\n  -print-file-name=libgcc.a) printf '%s\\n' '$bootlin_root/runtime/libgcc.a' ;;\n  *) exit 1 ;;\nesac"

bootlin_description=$(CPKT_TOOLCHAIN_CACHE="$cache" "$bootlin" discover x86_64-linux-gnu)
require_line 'source=bootlin' "$bootlin_description"
require_line 'status=ready' "$bootlin_description"
require_line "cc=$bootlin_root/bin/x86_64-linux-gcc" "$bootlin_description"
require_line "ld=$bootlin_root/bin/x86_64-linux-ld" "$bootlin_description"
require_line "libstdcxx_a=$bootlin_root/runtime/libstdc++.a" "$bootlin_description"
bootlin_env=$(CPKT_TOOLCHAIN_CACHE="$cache" "$bootlin" env x86_64-linux-gnu)
grep -Fq "export CC=$bootlin_root/bin/x86_64-linux-gcc" <<<"$bootlin_env" || fail 'Bootlin environment omitted the compiler'
grep -Fq "export LD=$bootlin_root/bin/x86_64-linux-ld" <<<"$bootlin_env" || fail 'Bootlin environment omitted the linker'

darwin_root="$cache/osxcross"
darwin_prefix=arm64-apple-darwin25.4
mkdir -p "$darwin_root/bin"
for tool in clang clang++ ld ar ranlib strip nm otool; do
  make_executable "$darwin_root/bin/$darwin_prefix-$tool" '#!/bin/sh\nexit 0'
  make_executable "$darwin_root/bin/arm64-apple-darwin25.3-$tool" '#!/bin/sh\nexit 0'
done
make_executable "$darwin_root/bin/arm64-apple-darwin25.5-clang" '#!/bin/sh\nexit 0'
host_mig_revision=88753c478c97b9a08bcdb66cecc68ba5881ff3af
host_mig_root="$cache/roots/host-mig-puredarwin-$host_mig_revision-x86_64-linux-gnu"
darwin_missing=$(OSXCROSS_ROOT="$darwin_root" CPKT_OSXCROSS_HOST="$darwin_prefix" CPKT_TOOLCHAIN_CACHE="$cache" "$bootlin" discover arm64-apple-darwin)
require_line 'status=missing' "$darwin_missing"
require_line "prefix=$darwin_prefix" "$darwin_missing"
require_line "mig_revision=$host_mig_revision" "$darwin_missing"
mkdir -p "$host_mig_root/bin" "$host_mig_root/libexec"
make_executable "$host_mig_root/bin/mig" '#!/bin/sh\nexit 0'
make_executable "$host_mig_root/bin/mig-upstream" '#!/bin/sh\nexit 0'
make_executable "$host_mig_root/libexec/migcom" '#!/bin/sh\nprintf "%s\\n" cpkt-host-mig'
printf 'component=host-mig\n' > "$host_mig_root/TOOLCHAIN"
darwin_description=$(OSXCROSS_ROOT="$darwin_root" CPKT_OSXCROSS_HOST="$darwin_prefix" CPKT_TOOLCHAIN_CACHE="$cache" "$bootlin" discover arm64-apple-darwin)
require_line 'source=osxcross+bootlin-host-mig' "$darwin_description"
require_line 'status=ready' "$darwin_description"
require_line "prefix=$darwin_prefix" "$darwin_description"
require_line "mig=$host_mig_root/bin/mig" "$darwin_description"
require_line "migcom=$host_mig_root/libexec/migcom" "$darwin_description"
require_line "mig_revision=$host_mig_revision" "$darwin_description"
OSXCROSS_ROOT="$darwin_root" CPKT_OSXCROSS_HOST="$darwin_prefix" CPKT_TOOLCHAIN_CACHE="$cache" "$bootlin" ensure arm64-apple-darwin >/dev/null
darwin_latest=$(env -u CPKT_OSXCROSS_HOST OSXCROSS_ROOT="$darwin_root" CPKT_TOOLCHAIN_CACHE="$cache" "$bootlin" discover arm64-apple-darwin)
require_line "prefix=$darwin_prefix" "$darwin_latest"
darwin_pinned=$(OSXCROSS_ROOT="$darwin_root" CPKT_OSXCROSS_HOST=arm64-apple-darwin25.3 CPKT_TOOLCHAIN_CACHE="$cache" "$bootlin" discover arm64-apple-darwin)
require_line 'prefix=arm64-apple-darwin25.3' "$darwin_pinned"

make_executable "$fake_bin/curl" '#!/bin/sh
while [ "$#" -gt 0 ]; do
  case "$1" in
    --output) output=$2; shift 2 ;;
    *) shift ;;
  esac
done
mkdir -p "$(dirname "$output")"
: > "$output"
printf "%s\\n" "simulated download failure" >&2
exit 42'

set +e
download_output=$(PATH="$fake_bin:$PATH" CPKT_TOOLCHAIN_CACHE="$failure_cache" "$bootlin" ensure x86_64-linux-gnu 2>&1)
download_status=$?
set -e
[[ $download_status -eq 42 ]] || fail "download failure status was $download_status, expected 42"
require_text 'simulated download failure' "$download_output"
[[ "$download_output" != *'unbound variable'* ]] || fail 'download cleanup masked the original failure'
if find "$failure_cache/archives" -maxdepth 1 -name '*.tmp.*' -print -quit | grep -q .; then
  fail 'download cleanup left a temporary archive'
fi
[[ -f "$failure_cache/locks/bootlin-$bootlin_name.lock" ]] || fail 'Bootlin provisioning did not create its collection lock'

bootlin_archive="$failure_cache/archives/$bootlin_name.tar.xz"
printf '%s\n' 'corrupt cached archive' > "$bootlin_archive"
make_executable "$fake_bin/curl" '#!/bin/sh
while [ "$#" -gt 0 ]; do
  case "$1" in
    --output) output=$2; shift 2 ;;
    *) shift ;;
  esac
done
mkdir -p "$(dirname "$output")"
printf "%s\\n" "replacement archive" > "$output"'
make_executable "$fake_bin/sha256sum" '#!/bin/sh
case "$1" in
  *.tmp.*) printf "%s  %s\\n" "cde893afab04ac7dcd15c46aac214ff550441b982536124c88a71146a0eeedd3" "$1" ;;
  *) printf "%s  %s\\n" "corrupt" "$1" ;;
esac'
make_executable "$fake_bin/tar" '#!/bin/sh
printf "%s\\n" "simulated extraction failure" >&2
exit 73'

set +e
extract_output=$(PATH="$fake_bin:$PATH" CPKT_TOOLCHAIN_CACHE="$failure_cache" "$bootlin" ensure x86_64-linux-gnu 2>&1)
extract_status=$?
set -e
[[ $extract_status -eq 73 ]] || fail "extraction failure status was $extract_status, expected 73"
require_text 'discarding corrupt cached archive' "$extract_output"
require_text 'simulated extraction failure' "$extract_output"
[[ "$extract_output" != *'unbound variable'* ]] || fail 'extraction cleanup masked the original failure'
grep -Fxq 'replacement archive' "$bootlin_archive" || fail 'corrupt archive was not replaced before extraction'
if find "$failure_cache/roots" -maxdepth 1 -name '.extract-*' -print -quit | grep -q .; then
  fail 'extraction cleanup left a temporary directory'
fi

printf '[test] pinned toolchain resolvers passed\n'
