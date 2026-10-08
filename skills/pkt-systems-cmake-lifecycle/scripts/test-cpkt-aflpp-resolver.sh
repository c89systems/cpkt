#!/usr/bin/env bash
set -euo pipefail
skill_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
resolver="$skill_dir/scripts/cpkt-aflpp.sh"
work=$(mktemp -d "$TMPDIR/afl-fixture.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
fail() { printf 'AFL fixture: %s\n' "$*" >&2; exit 1; }

# Local synthetic collections exercise discovery; they never download or compile.
id=x86-64--glibc--stable-2026.08-1
collection="$work/cache/roots/$id"
sysroot="$collection/x86_64-buildroot-linux-gnu/sysroot"
mkdir -p "$collection/bin" "$collection/lib" "$collection/runtime" "$sysroot/lib" "$sysroot/usr/lib" "$sysroot/usr/include"
touch "$sysroot/usr/include/stdio.h" "$sysroot/usr/lib/libc.so"
touch "$collection/runtime/libstdc++.a" "$collection/runtime/libgcc.a" "$collection/runtime/libstdc++.so.6"
for tool in gcc g++ ld ar ranlib strip nm objcopy objdump addr2line gdb readelf cc1 cc1plus; do
  printf '#!/bin/sh\nexit 0\n' > "$collection/bin/x86_64-linux-$tool"
  chmod +x "$collection/bin/x86_64-linux-$tool"
done
cat > "$collection/bin/x86_64-linux-g++" <<'COMPILER'
#!/bin/sh
case "$1" in
  -print-file-name=libstdc++.a) printf '%s/runtime/libstdc++.a\n' "$TEST_COLLECTION";;
  -print-file-name=libgcc.a) printf '%s/runtime/libgcc.a\n' "$TEST_COLLECTION";;
  -print-file-name=libstdc++.so.6) printf '%s/runtime/libstdc++.so.6\n' "$TEST_COLLECTION";;
  -print-prog-name=cc1) printf '%s/bin/x86_64-linux-cc1\n' "$TEST_COLLECTION";;
  -print-prog-name=cc1plus) printf '%s/bin/x86_64-linux-cc1plus\n' "$TEST_COLLECTION";;
  *) exit 1;;
esac
COMPILER
cp "$collection/bin/x86_64-linux-g++" "$collection/bin/x86_64-linux-gcc"
printf '#!/bin/sh\nexit 0\n' > "$sysroot/lib/ld-linux-x86-64.so.2"
chmod +x "$sysroot/lib/ld-linux-x86-64.so.2"
export TEST_COLLECTION="$collection" CPKT_TOOLCHAIN_CACHE="$work/cache"
prepared="$work/cache/roots/aflplusplus-5.02c-x86_64-linux-gnu-$id-r2"
mkdir -p "$prepared/bin" "$prepared/lib/afl" "$prepared/libexec/bootlin-runtime"
for tool in afl-fuzz afl-showmap cpkt-afl-gcc cpkt-afl-g++ afl-cc bootlin-gcc bootlin-g++; do
  printf '#!/bin/sh\nexit 0\n' > "$prepared/bin/$tool"
  chmod +x "$prepared/bin/$tool"
done
ln -s afl-cc "$prepared/bin/afl-gcc-fast"
ln -s afl-cc "$prepared/bin/afl-g++-fast"
for tool in cc1 cc1plus; do
  printf '#!/bin/sh\nexit 0\n' > "$prepared/libexec/bootlin-runtime/$tool"
  chmod +x "$prepared/libexec/bootlin-runtime/$tool"
done
touch "$prepared/lib/afl/afl-gcc-pass.so" "$prepared/lib/afl/afl-compiler-rt.o" "$prepared/.cpkt-aflpp-revision-2-$id"
description=$("$resolver" discover)
grep -Fxq 'status=ready' <<< "$description" || fail 'prepared inputs unavailable'
environment=$("$resolver" env)
(
  eval "$environment"
  [[ "$CC" = "$prepared/bin/cpkt-afl-gcc" && "$AFL_CC" = "$prepared/bin/bootlin-gcc" ]] ||
    fail 'wrong instrumentation tools selected'
)
for required in "$prepared/bin/afl-showmap" "$sysroot/lib/ld-linux-x86-64.so.2"; do
  mv "$required" "$required.saved"
  for mode in discover env; do
    if "$resolver" "$mode" > "$work/out" 2> "$work/err"; then fail 'missing input accepted'; fi
    [[ ! -s "$work/out" && ! -e "$required" ]] || fail 'query repaired or exported missing state'
  done
  mv "$required.saved" "$required"
done
printf 'AFL discovery/runtime contract passed\n'
