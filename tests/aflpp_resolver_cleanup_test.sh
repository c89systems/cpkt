#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: aflpp_resolver_cleanup_test.sh <source-dir>\n' >&2
  exit 2
fi

source_dir=$1
mkdir -p "$source_dir/build/fixtures"
work_dir=$(mktemp -d "$source_dir/build/fixtures/afl-resolver.XXXXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
fake_repo="$work_dir/repo"
fake_bin="$work_dir/bin"
cache_root="$work_dir/cache root"
bootlin_root="$work_dir/bootlin-v1"
compiler_arguments="$work_dir/compiler-arguments"
mkdir -p "$fake_repo/scripts" "$fake_bin" "$bootlin_root/include" "$cache_root/archives"
mkdir -p "$bootlin_root/bin" "$bootlin_root/libexec" "$bootlin_root/lib64" "$bootlin_root/sysroot/lib"
cp "$source_dir/scripts/cpkt-aflpp.sh" "$fake_repo/scripts/cpkt-aflpp.sh"
cp "$source_dir/scripts/cpkt-archive-cache.sh" "$fake_repo/scripts/cpkt-archive-cache.sh"
cp "$source_dir/scripts/cpkt-afl-runtime.sh" "$fake_repo/scripts/cpkt-afl-runtime.sh"
cp "$source_dir/scripts/require-host-bash.sh" "$fake_repo/scripts/require-host-bash.sh"
chmod +x "$fake_repo/scripts/cpkt-aflpp.sh"
grep -Fq 'with_cache_lock "$cache/locks/aflplusplus-${version}-x86_64-linux-gnu.lock" ensure_locked "$cache"' "$fake_repo/scripts/cpkt-aflpp.sh" || {
  printf 'AFL++ root publication is not serialized by a shared cache lock\n' >&2
  exit 1
}
grep -Fq 'afl_ready "$root" "$collection_id" && return' "$fake_repo/scripts/cpkt-aflpp.sh" || {
  printf 'AFL++ root readiness is not rechecked after acquiring the shared cache lock\n' >&2
  exit 1
}

cat > "$fake_repo/scripts/cpkt-toolchains.sh" <<EOF
#!/bin/sh
case "\$1" in
  ensure) printf 'ensure\\n' >> "$work_dir/bootlin-calls"; exit 0 ;;
  discover)
    printf 'status=%s\\n' "\${CPKT_TEST_BOOTLIN_STATUS:-ready}"
    printf 'cc=%s\\n' '$bootlin_root/bin/cc'
    printf 'cxx=%s\\n' '$bootlin_root/bin/cxx'
    printf 'root=%s\\n' '$bootlin_root'
    printf 'sysroot=%s\\n' '$bootlin_root/sysroot'
    ;;
  *) exit 2 ;;
esac
EOF
chmod +x "$fake_repo/scripts/cpkt-toolchains.sh"

cat > "$fake_bin/sha256sum" <<'EOF'
#!/bin/sh
if [ "$1" = -c ]; then
  cat >/dev/null
else
  printf '%s  %s\n' 118415843e5d289d63bd6d8f2252c18212978f15ac9e86acbbc75766cd45acde "$1"
fi
exit 0
EOF
cat > "$fake_bin/tar" <<'EOF'
#!/bin/sh
while [ "$#" -gt 0 ]; do
  case "$1" in
    -C) destination=$2; shift 2 ;;
    *) shift ;;
  esac
done
mkdir -p "$destination/AFLplusplus-5.02c"
EOF
cat > "$fake_bin/make" <<'EOF'
#!/bin/sh
printf '%s\n' "$@" >> "$CPKT_TEST_MAKE_ARGUMENTS"
exit 0
EOF
cat > "$bootlin_root/bin/cc" <<'EOF'
#!/bin/sh
case "$1" in
  -print-file-name=libstdc++.so.6) printf '%s/lib64/libstdc++.so.6\n' "$CPKT_TEST_BOOTLIN_ROOT"; exit 0 ;;
  -print-prog-name=cc1) printf '%s/libexec/cc1\n' "$CPKT_TEST_BOOTLIN_ROOT"; exit 0 ;;
  -print-prog-name=cc1plus) printf '%s/libexec/cc1plus\n' "$CPKT_TEST_BOOTLIN_ROOT"; exit 0 ;;
esac
printf '%s\n' "$@" > "$CPKT_TEST_COMPILER_ARGUMENTS"
printf '%s\n' 'simulated AFL++ compiler failure' >&2
exit 73
EOF
cp "$bootlin_root/bin/cc" "$bootlin_root/bin/cxx"
printf '#!/bin/sh\nshift 2\nexec "$@"\n' > "$bootlin_root/sysroot/lib/ld-linux-x86-64.so.2"
touch "$bootlin_root/lib64/libstdc++.so.6"
for backend in cc1 cc1plus; do
  printf '#!/bin/sh\nexit 0\n' > "$bootlin_root/libexec/$backend"
  chmod +x "$bootlin_root/libexec/$backend"
done
chmod +x "$fake_bin/sha256sum" "$fake_bin/tar" "$fake_bin/make" "$bootlin_root/bin/cc" "$bootlin_root/bin/cxx" "$bootlin_root/sysroot/lib/ld-linux-x86-64.so.2"

# Discovery and environment inspection fail without provisioning or downloads.
cat > "$fake_bin/curl" <<EOF
#!/bin/sh
: > "$work_dir/download-called"
exit 89
EOF
cp "$fake_bin/curl" "$fake_bin/wget"
chmod +x "$fake_bin/curl" "$fake_bin/wget"
missing_cache="$work_dir/missing-cache"
for mode in discover env; do
  if output=$(PATH="$fake_bin:$PATH" CPKT_TOOLCHAIN_CACHE="$missing_cache" "$fake_repo/scripts/cpkt-aflpp.sh" "$mode" 2>&1); then
    printf 'AFL++ %s accepted missing tools\n' "$mode" >&2; exit 1
  fi
  case "$output" in *'cpkt-aflpp.sh ensure'*) ;; *) printf '%s\n' "$output" >&2; exit 1 ;; esac
  if [[ -e "$missing_cache" || -e "$work_dir/bootlin-calls" || -e "$work_dir/download-called" ]]; then
    printf 'AFL++ inspection provisioned tools\n' >&2; exit 1
  fi
  if output=$(PATH="$fake_bin:$PATH" CPKT_TEST_BOOTLIN_STATUS=missing CPKT_TOOLCHAIN_CACHE="$missing_cache" "$fake_repo/scripts/cpkt-aflpp.sh" "$mode" 2>&1); then
    printf 'AFL++ %s accepted missing Bootlin\n' "$mode" >&2; exit 1
  fi
  case "$output" in *'cpkt-toolchains.sh ensure'*) ;; *) printf '%s\n' "$output" >&2; exit 1 ;; esac
  case "$output" in
    *'collection identity contains'*|*'pinned AFL++ collection is unavailable'*)
      printf 'inspection continued after failed Bootlin discovery\n%s\n' "$output" >&2; exit 1 ;;
  esac
  [[ ! -e "$missing_cache" && ! -e "$work_dir/bootlin-calls" && ! -e "$work_dir/download-called" ]]
done
prepared="$missing_cache/roots/aflplusplus-5.02c-x86_64-linux-gnu-bootlin-v1"
mkdir -p "$prepared/bin" "$prepared/lib/afl" "$prepared/libexec/bootlin-runtime"
for executable in afl-fuzz afl-showmap cpkt-afl-gcc cpkt-afl-g++ afl-cc afl-gcc-fast afl-g++-fast bootlin-gcc bootlin-g++; do
  printf '#!/bin/sh\nexit 0\n' > "$prepared/bin/$executable"
  chmod +x "$prepared/bin/$executable"
done
for backend in cc1 cc1plus; do
  printf '#!/bin/sh\nexit 0\n' > "$prepared/libexec/bootlin-runtime/$backend"
  chmod +x "$prepared/libexec/bootlin-runtime/$backend"
done
touch "$prepared/.cpkt-aflpp-revision-2-bootlin-v1" "$prepared/lib/afl/afl-gcc-pass.so" "$prepared/lib/afl/afl-compiler-rt.o"
for mode in discover env; do
  output=$(PATH="$fake_bin:$PATH" CPKT_TOOLCHAIN_CACHE="$missing_cache" "$fake_repo/scripts/cpkt-aflpp.sh" "$mode")
  case "$output" in *"$prepared"*) ;; *) printf 'inspection selected wrong collection\n' >&2; exit 1 ;; esac
done
for required in bin/afl-fuzz bin/afl-showmap bin/cpkt-afl-gcc bin/cpkt-afl-g++ bin/afl-cc bin/afl-gcc-fast bin/afl-g++-fast bin/bootlin-gcc bin/bootlin-g++ libexec/bootlin-runtime/cc1 libexec/bootlin-runtime/cc1plus lib/afl/afl-gcc-pass.so lib/afl/afl-compiler-rt.o .cpkt-aflpp-revision-2-bootlin-v1; do
  mv "$prepared/$required" "$work_dir/removed-tool"
  for mode in discover env; do
    if PATH="$fake_bin:$PATH" CPKT_TOOLCHAIN_CACHE="$missing_cache" "$fake_repo/scripts/cpkt-aflpp.sh" "$mode" >/dev/null 2>&1; then
      printf 'inspection accepted incomplete collection: %s\n' "$required" >&2; exit 1
    fi
  done
  mv "$work_dir/removed-tool" "$prepared/$required"
done
[[ ! -e "$work_dir/bootlin-calls" && ! -e "$work_dir/download-called" ]]

old_root="$cache_root/roots/aflplusplus-5.02c-x86_64-linux-gnu"
mkdir -p "$old_root/bin" "$old_root/lib/afl"
for executable in afl-fuzz afl-showmap cpkt-afl-gcc cpkt-afl-g++; do
  printf '#!/bin/sh\nexit 0\n' > "$old_root/bin/$executable"
  chmod +x "$old_root/bin/$executable"
done
touch "$old_root/.cpkt-aflpp-revision-1" "$old_root/lib/afl/afl-gcc-pass.so" "$old_root/lib/afl/afl-compiler-rt.o"

: > "$cache_root/archives/AFLplusplus-5.02c.tar.gz"
set +e
output=$(PATH="$fake_bin:$PATH" CPKT_TOOLCHAIN_CACHE="$cache_root" CPKT_TEST_BOOTLIN_ROOT="$bootlin_root" CPKT_TEST_MAKE_ARGUMENTS="$work_dir/make-arguments" CPKT_TEST_COMPILER_ARGUMENTS="$compiler_arguments" "$fake_repo/scripts/cpkt-aflpp.sh" ensure 2>&1)
status=$?
set -e
if [[ $status -ne 73 ]]; then
  printf 'AFL++ build failure status was %s, expected 73\n%s\n' "$status" "$output" >&2
  exit 1
fi
case "$output" in
  *'simulated AFL++ compiler failure'*) ;;
  *) printf 'AFL++ compiler failure was not preserved\n%s\n' "$output" >&2; exit 1 ;;
esac
case "$output" in
  *'unbound variable'*) printf 'AFL++ cleanup masked the build failure\n%s\n' "$output" >&2; exit 1 ;;
esac
if find "$cache_root/roots" -maxdepth 1 -name 'aflplusplus-*.tmp.*' -print -quit | grep -q .; then
  printf 'failed AFL++ provisioning left a staging directory in the shared cache\n' >&2
  exit 1
fi
if find "$cache_root" -maxdepth 1 -name '.aflplusplus-extract.*' -print -quit | grep -q .; then
  printf 'failed AFL++ provisioning left an extraction directory in the shared cache\n' >&2
  exit 1
fi
if [ ! -f "$cache_root/locks/aflplusplus-5.02c-x86_64-linux-gnu.lock" ]; then
  printf 'AFL++ provisioning did not create its shared cache lock\n' >&2
  exit 1
fi
expected_helper="$cache_root/roots/aflplusplus-5.02c-x86_64-linux-gnu-bootlin-v1/lib/afl"
grep -Fx -- '-B' "$compiler_arguments" >/dev/null || {
  printf 'AFL++ build bypassed the Bootlin runtime compiler backend route\n' >&2; exit 1
}
if ! grep -F -- '--dynamic-linker' "$work_dir/make-arguments" >/dev/null ||
   ! grep -F -- "$bootlin_root/sysroot/lib/ld-linux-x86-64.so.2" "$work_dir/make-arguments" >/dev/null; then
  printf 'AFL++ binaries were built without the selected Bootlin interpreter\n' >&2; exit 1
fi
grep -F -- '--disable-new-dtags' "$work_dir/make-arguments" >/dev/null || {
  printf 'AFL++ binaries were built without transitive private runtime paths\n' >&2; exit 1
}
grep -Fx -- "-DAFL_PATH=\"$expected_helper\"" "$compiler_arguments" >/dev/null || {
  printf 'AFL++ compiler arguments did not preserve the cache path as one quoted definition\n' >&2
  exit 1
}

printf '[test] AFL++ resolver cleanup passed\n'
