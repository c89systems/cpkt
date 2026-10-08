#!/usr/bin/env bash
set -euo pipefail

skill_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
bootlin_resolver="$skill_dir/scripts/cpkt-toolchains.sh"
cache=$(mktemp -d "${TMPDIR:?CTest must set repository-local fixture scratch}/cpkt-toolchain-test.XXXXXX")
trap 'rm -rf -- "$cache" "$cache.alias"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

fail() {
  printf 'test-cpkt-toolchain-resolvers: %s\n' "$*" >&2
  exit 1
}

grep -Fq 'with_cache_lock "$(cache_root)/locks/bootlin-$name.lock" install_bootlin_locked "$target"' "$bootlin_resolver" || fail 'Bootlin root publication is not serialized'
grep -Fq 'if bootlin_ready "$root" "$prefix" "$root/$sysroot_rel"; then' "$bootlin_resolver" || fail 'Bootlin root readiness is not rechecked under the lock'

require_line() {
  local expected=$1 output=$2
  printf '%s\n' "$output" | grep -Fqx "$expected" || fail "missing output: $expected"
}

make_executable() {
  printf '%b\n' "$2" > "$1"
  chmod +x "$1"
}

make_bootlin_collection() {
  local name=$1 prefix=$2 sysroot_rel=$3 root sysroot tool
  root="$cache/roots/$name"
  sysroot="$root/$sysroot_rel"
  mkdir -p "$root/bin" "$sysroot/usr/include" "$sysroot/usr/lib" "$root/runtime"
  : > "$sysroot/usr/include/stdio.h"
  : > "$sysroot/usr/lib/libc.so"
  : > "$root/runtime/libstdc++.a"
  : > "$root/runtime/libgcc.a"
  for tool in gcc ld ar ranlib strip nm objcopy objdump addr2line gdb readelf; do
    make_executable "$root/bin/$prefix-$tool" '#!/bin/sh\nexit 0'
  done
  make_executable "$root/bin/$prefix-g++" "#!/bin/sh\nruntime_root=\${CPKT_TEST_RUNTIME_ROOT:-'$root/runtime'}\ncase \"\$1\" in\n  -print-file-name=libstdc++.a) printf '%s/libstdc++.a\\n' \"\$runtime_root\" ;;\n  -print-file-name=libgcc.a) printf '%s/libgcc.a\\n' \"\$runtime_root\" ;;\n  *) exit 1 ;;\nesac"
}

make_bootlin_collection \
  x86-64--glibc--stable-2026.08-1 \
  x86_64-linux \
  x86_64-buildroot-linux-gnu/sysroot

bootlin_description=$(CPKT_TOOLCHAIN_CACHE="$cache" "$bootlin_resolver" discover x86_64-linux-gnu)
require_line 'source=bootlin' "$bootlin_description"
require_line 'status=ready' "$bootlin_description"
require_line "cc=$cache/roots/x86-64--glibc--stable-2026.08-1/bin/x86_64-linux-gcc" "$bootlin_description"
require_line "ld=$cache/roots/x86-64--glibc--stable-2026.08-1/bin/x86_64-linux-ld" "$bootlin_description"
require_line "nm=$cache/roots/x86-64--glibc--stable-2026.08-1/bin/x86_64-linux-nm" "$bootlin_description"
bootlin_env=$(CPKT_TOOLCHAIN_CACHE="$cache" "$bootlin_resolver" env x86_64-linux-gnu)
printf '%s\n' "$bootlin_env" | grep -Fq "export CC=$cache/roots/x86-64--glibc--stable-2026.08-1/bin/x86_64-linux-gcc" || fail 'Bootlin env did not export the pinned compiler'
printf '%s\n' "$bootlin_env" | grep -Fq "export LD=$cache/roots/x86-64--glibc--stable-2026.08-1/bin/x86_64-linux-ld" || fail 'Bootlin env did not export the pinned linker'
printf '%s\n' "$bootlin_env" | grep -Fq "export NM=$cache/roots/x86-64--glibc--stable-2026.08-1/bin/x86_64-linux-nm" || fail 'Bootlin env did not export the pinned nm'

expect_unready() {
  local description
  description=$(CPKT_TOOLCHAIN_CACHE="$cache" "$bootlin_resolver" discover x86_64-linux-gnu)
  require_line 'status=missing' "$description"
  if CPKT_TOOLCHAIN_CACHE="$cache" "$bootlin_resolver" env x86_64-linux-gnu > "$cache/env.out" 2> "$cache/env.err"; then
    fail 'environment accepted an invalid collection'
  fi
  [[ ! -s "$cache/env.out" ]] || fail 'invalid collection exported tools'
  grep -Fq 'ensure x86_64-linux-gnu' "$cache/env.err" || fail 'missing preparation diagnostic'
}
root="$cache/roots/x86-64--glibc--stable-2026.08-1"
sysroot="$root/x86_64-buildroot-linux-gnu/sysroot"
foreign="$root.foreign"
mkdir -p "$foreign"
touch "$foreign/libstdc++.a" "$foreign/libgcc.a" "$foreign/stdio.h" "$foreign/libc.so"
export CPKT_TEST_FOREIGN_CALLS="$cache/foreign-calls"
printf '#!/bin/sh\nprintf "executed\\n" >> "$CPKT_TEST_FOREIGN_CALLS"\n' > "$foreign/tool"
tail -n +2 "$root/bin/x86_64-linux-g++" >> "$foreign/tool"
chmod +x "$foreign/tool"
CPKT_TEST_RUNTIME_ROOT="$foreign" expect_unready
for tool in gcc g++ ld ar ranlib strip nm objcopy objdump addr2line gdb readelf; do
  path="$root/bin/x86_64-linux-$tool"
  mv "$path" "$path.saved"
  ln -s "$foreign/tool" "$path"
  expect_unready
  [[ ! -e "$CPKT_TEST_FOREIGN_CALLS" ]] || fail 'discovery executed a foreign compiler'
  rm "$path"
  mv "$path.saved" "$path"
done
for name in libstdc++.a libgcc.a; do
  path="$root/runtime/$name"
  mv "$path" "$path.real"
  ln -s "$foreign/$name" "$path"
  expect_unready
  rm "$path"
  ln -s "$name.real" "$path"
  description=$(CPKT_TOOLCHAIN_CACHE="$cache" "$bootlin_resolver" discover x86_64-linux-gnu)
  require_line 'status=ready' "$description"
  case "$name" in libstdc++.a) key=libstdcxx_a;; libgcc.a) key=libgcc_a;; esac
  require_line "$key=$path.real" "$description"
  rm "$path"
  mv "$path.real" "$path"
done
for path in "$sysroot/usr/include/stdio.h" "$sysroot/usr/lib/libc.so"; do
  mv "$path" "$path.saved"
  ln -s "$foreign/${path##*/}" "$path"
  expect_unready
  rm "$path"
  mv "$path.saved" "$path"
done
mv "$root/bin/x86_64-linux-gcc" "$root/bin/gcc.real"
ln -s gcc.real "$root/bin/x86_64-linux-gcc"
description=$(CPKT_TOOLCHAIN_CACHE="$cache" "$bootlin_resolver" discover x86_64-linux-gnu)
require_line 'status=ready' "$description"
ln -s "$cache" "$cache.alias"
description=$(CPKT_TOOLCHAIN_CACHE="$cache.alias" "$bootlin_resolver" discover x86_64-linux-gnu)
require_line 'status=ready' "$description"
require_line "libstdcxx_a=$root/runtime/libstdc++.a" "$description"
rm "$cache.alias"

printf 'toolchain resolver tests passed\n'
