#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: aflpp_resolver_cleanup_test.sh <source-dir>\n' >&2
  exit 2
fi

source_dir=$1
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
fake_repo="$work_dir/repo"
fake_bin="$work_dir/bin"
cache_root="$work_dir/cache root"
bootlin_root="$work_dir/bootlin-v1"
compiler_arguments="$work_dir/compiler-arguments"
mkdir -p "$fake_repo/scripts" "$fake_bin" "$bootlin_root/include" "$cache_root/archives"
cp "$source_dir/scripts/cpkt-aflpp.sh" "$fake_repo/scripts/cpkt-aflpp.sh"
cp "$source_dir/scripts/cpkt-archive-cache.sh" "$fake_repo/scripts/cpkt-archive-cache.sh"
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
  ensure) exit 0 ;;
  discover)
    printf 'cc=%s\\n' '$fake_bin/cc'
    printf 'cxx=%s\\n' '$fake_bin/cxx'
    printf 'root=%s\\n' '$bootlin_root'
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
exit 0
EOF
cat > "$fake_bin/cc" <<'EOF'
#!/bin/sh
printf '%s\n' "$@" > "$CPKT_TEST_COMPILER_ARGUMENTS"
printf '%s\n' 'simulated AFL++ compiler failure' >&2
exit 73
EOF
printf '#!/bin/sh\nexit 0\n' > "$fake_bin/cxx"
chmod +x "$fake_bin/sha256sum" "$fake_bin/tar" "$fake_bin/make" "$fake_bin/cc" "$fake_bin/cxx"

old_root="$cache_root/roots/aflplusplus-5.02c-x86_64-linux-gnu"
mkdir -p "$old_root/bin" "$old_root/lib/afl"
for executable in afl-fuzz afl-showmap cpkt-afl-gcc cpkt-afl-g++; do
  printf '#!/bin/sh\nexit 0\n' > "$old_root/bin/$executable"
  chmod +x "$old_root/bin/$executable"
done
touch "$old_root/.cpkt-aflpp-revision-1" "$old_root/lib/afl/afl-gcc-pass.so" "$old_root/lib/afl/afl-compiler-rt.o"

: > "$cache_root/archives/AFLplusplus-5.02c.tar.gz"
set +e
output=$(PATH="$fake_bin:$PATH" CPKT_TOOLCHAIN_CACHE="$cache_root" CPKT_TEST_COMPILER_ARGUMENTS="$compiler_arguments" "$fake_repo/scripts/cpkt-aflpp.sh" ensure 2>&1)
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
grep -Fx -- "-DAFL_PATH=\"$expected_helper\"" "$compiler_arguments" >/dev/null || {
  printf 'AFL++ compiler arguments did not preserve the cache path as one quoted definition\n' >&2
  exit 1
}

printf '[test] AFL++ resolver cleanup passed\n'
