#!/usr/bin/env bash
set -euo pipefail

skill_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
resolver="$skill_dir/scripts/cpkt-aflpp.sh"
fail() { printf 'test-cpkt-aflpp-resolver: %s\n' "$*" >&2; exit 1; }

[[ -x "$resolver" ]] || fail 'resolver is not executable'
bash -n "$resolver"
grep -Fq 'version=5.02c' "$resolver" || fail 'AFL++ version is not pinned'
grep -Fq 'archive_sha256=' "$resolver" || fail 'AFL++ checksum is not pinned'
grep -Fq 'cpkt-toolchains.sh' "$resolver" || fail 'resolver does not use the embedded Bootlin resolver'
grep -Fq 'install_cleanup_trap -f "$dl"' "$resolver" || fail 'resolver does not clean interrupted AFL++ downloads'
grep -Fq 'install_cleanup_trap -rf "$tmp"' "$resolver" || fail 'resolver does not clean failed staging state'
grep -Fq 'with_cache_lock "$c/locks/aflplusplus-${version}-x86_64-linux-gnu.lock" ensure_locked' "$resolver" || fail 'resolver does not serialize shared AFL++ publication'
grep -Fq 'collection_id()' "$resolver" || fail 'resolver does not key AFL++ caches by Bootlin collection identity'
grep -Fq '.cpkt-aflpp-revision-$revision-$id' "$resolver" || fail 'resolver readiness marker is not tied to the Bootlin collection identity'
grep -Fq 'ready "$r" "$id" && return' "$resolver" || fail 'resolver does not recheck collection-specific AFL++ readiness under the lock'
grep -Fq '"-DAFL_PATH=\"$helper\""' "$resolver" || fail 'resolver does not preserve AFL++ cache paths as one compiler argument'
grep -Fq 'export PATH=%q' "$resolver" || fail 'resolver env output does not prepend the pinned AFL++ bin directory'

fake_bin=$(mktemp -d "${TMPDIR:?CTest must set repository-local fixture scratch}/cpkt-aflpp-test.XXXXXX")
trap 'rm -rf "$fake_bin"' EXIT HUP INT TERM
printf '#!/bin/sh\nprintf "aarch64\\n"\n' > "$fake_bin/uname"
chmod +x "$fake_bin/uname"
if PATH="$fake_bin:$PATH" "$resolver" ensure >/dev/null 2>&1; then
  fail 'resolver accepted a non-native host'
fi

if env -u HOME -u XDG_CACHE_HOME -u CPKT_TOOLCHAIN_CACHE "$resolver" discover >/dev/null 2>&1; then
  fail 'resolver accepted a missing cache root'
fi

if "$resolver" ensure extra >/dev/null 2>&1; then
  fail 'resolver accepted an invalid command shape'
fi

signal_root=$(mktemp -d "$TMPDIR/cpkt-aflpp-signal-test.XXXXXX")
trap 'rm -rf "$fake_bin" "$signal_root"' EXIT HUP INT TERM
signal_skill="$signal_root/skill"
signal_bin="$signal_root/bin"
signal_cache="$signal_root/cache"
signal_bootlin="$signal_root/runtime collection/bootlin"
mkdir -p "$signal_skill/scripts" "$signal_bin" "$signal_bootlin/include"
cp "$resolver" "$signal_skill/scripts/cpkt-aflpp.sh"
cp "$skill_dir/scripts/cpkt-archive-cache.sh" "$signal_skill/scripts/cpkt-archive-cache.sh"
cp "$skill_dir/scripts/cpkt-afl-runtime.sh" "$signal_skill/scripts/cpkt-afl-runtime.sh"
cp "$skill_dir/scripts/require-host-bash.sh" "$signal_skill/scripts/require-host-bash.sh"
chmod +x "$signal_skill/scripts/cpkt-aflpp.sh"
touch "$signal_bootlin/include/gmp.h"
printf '#!/bin/sh\nexit 0\n' > "$signal_bin/cc"
printf '#!/bin/sh\nexit 0\n' > "$signal_bin/cxx"
runtime_root="$signal_bootlin"
runtime_sysroot="$runtime_root/sysroot"
runtime_stage="$signal_root/runtime-stage"
mkdir -p "$runtime_root/bin" "$runtime_root/lib" "$runtime_sysroot/lib" "$runtime_sysroot/usr/lib"
touch "$runtime_root/lib/libstdc++.so.6"
for tool in cc1 cc1plus; do
  printf '#!/bin/sh\nexit 0\n' > "$runtime_root/bin/$tool"
  chmod +x "$runtime_root/bin/$tool"
done
cat > "$runtime_root/bin/compiler" <<'COMPILER'
#!/bin/sh
case "$1" in
  -print-file-name=libstdc++.so.6) printf '%s/lib/libstdc++.so.6\n' "$CPKT_TEST_RUNTIME_ROOT";;
  -print-prog-name=cc1) printf '%s/bin/cc1\n' "$CPKT_TEST_RUNTIME_ROOT";;
  -print-prog-name=cc1plus) printf '%s/bin/cc1plus\n' "$CPKT_TEST_RUNTIME_ROOT";;
  *) printf '%s\n' "$@" > "$CPKT_TEST_RUNTIME_CALLS";;
esac
COMPILER
cat > "$runtime_sysroot/lib/loader.real" <<'LOADER'
#!/bin/sh
[ "$1" = --library-path ] || exit 1
shift 2
exec "$@"
LOADER
chmod +x "$runtime_root/bin/compiler" "$runtime_sysroot/lib/loader.real"
ln -s loader.real "$runtime_sysroot/lib/ld-linux-x86-64.so.2"
export CPKT_TEST_RUNTIME_ROOT="$runtime_root"
export CPKT_TEST_RUNTIME_CALLS="$signal_root/runtime-calls"
cat > "$signal_skill/scripts/cpkt-toolchains.sh" <<EOF
#!/bin/sh
case "\$1" in
  ensure) printf 'ensure\\n' >> "\${CPKT_TEST_BOOTLIN_CALLS:?}"; exit 0 ;;
  discover)
    printf 'status=%s\\n' "\${CPKT_TEST_BOOTLIN_STATUS:-ready}"
    printf 'cc=%s\\n' '$signal_bootlin/bin/compiler'
    printf 'cxx=%s\\n' '$signal_bootlin/bin/compiler'
    printf 'root=%s\\n' '$signal_bootlin'
    printf 'sysroot=%s\\n' '$runtime_sysroot'
    ;;
  *) exit 2 ;;
esac
EOF
cat > "$signal_bin/sha256sum" <<'EOF'
#!/bin/sh
cat >/dev/null
exit 0
EOF
cat > "$signal_bin/curl" <<'EOF'
#!/bin/sh
while [ "$#" -gt 0 ]; do
  case "$1" in
    -o|--output) output=$2; shift 2 ;;
    *) shift ;;
  esac
done
mkdir -p "$(dirname -- "$output")"
: > "$output"
printf '%s\n' "$output" > "${CPKT_TEST_DOWNLOADER_MARKER:?}"
if [ "${CPKT_TEST_BLOCK_DOWNLOAD:-0}" = 1 ]; then
  printf '%s %s\n' "$$" "$PPID" > "$CPKT_TEST_BLOCKED_MARKER"
  trap 'printf "stopped\n" >> "$CPKT_TEST_BLOCKED_MARKER"; exit 1' TERM
  while :; do sleep 0.1; done
fi
kill -TERM "$PPID"
printf 'signalled\n' >> "$CPKT_TEST_DOWNLOADER_MARKER"
EOF
chmod +x "$signal_skill/scripts/cpkt-toolchains.sh" "$signal_bin/cc" "$signal_bin/cxx" "$signal_bin/sha256sum" "$signal_bin/curl"

# Discovery and environment extraction must not provision missing prerequisites.
export CPKT_TEST_BOOTLIN_CALLS="$signal_root/bootlin-calls"
export CPKT_TEST_DOWNLOADER_MARKER="$signal_root/downloader-ran"
for mode in discover env; do
  if CPKT_TEST_BOOTLIN_STATUS=missing CPKT_TOOLCHAIN_CACHE="$signal_cache" \
    "$signal_skill/scripts/cpkt-aflpp.sh" "$mode" > "$signal_root/$mode.out" 2> "$signal_root/$mode.err"; then
    fail "$mode accepted missing Bootlin"
  fi
  grep -Fq 'ensure x86_64-linux-gnu' "$signal_root/$mode.err" || fail "$mode omitted Bootlin preparation command"
  if CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" "$mode" \
    > "$signal_root/$mode.out" 2> "$signal_root/$mode.err"; then
    fail "$mode accepted missing AFL++"
  fi
  grep -Fq 'cpkt-aflpp.sh ensure' "$signal_root/$mode.err" || fail "$mode omitted AFL++ preparation command"
done
[[ ! -e "$CPKT_TEST_BOOTLIN_CALLS" && ! -e "$signal_cache" && ! -e "$CPKT_TEST_DOWNLOADER_MARKER" ]] || fail 'discovery/environment provisioned missing tools'

# Exercise usable, incomplete and wrong-collection prepared roots without builds.
prepared="$signal_cache/roots/aflplusplus-5.02c-x86_64-linux-gnu-bootlin-r2"
mkdir -p "$prepared/bin" "$prepared/lib/afl" "$prepared/libexec/bootlin-runtime"
for tool in afl-fuzz afl-showmap afl-cc cpkt-afl-gcc cpkt-afl-g++ bootlin-gcc bootlin-g++; do
  printf '#!/bin/sh\nexit 0\n' > "$prepared/bin/$tool"
  chmod +x "$prepared/bin/$tool"
done
ln -s afl-cc "$prepared/bin/afl-gcc-fast"
ln -s afl-cc "$prepared/bin/afl-g++-fast"
for tool in cc1 cc1plus; do
  printf '#!/bin/sh\nexit 0\n' > "$prepared/libexec/bootlin-runtime/$tool"
  chmod +x "$prepared/libexec/bootlin-runtime/$tool"
done
touch "$prepared/lib/afl/afl-gcc-pass.so" "$prepared/lib/afl/afl-compiler-rt.o" "$prepared/.cpkt-aflpp-revision-2-bootlin"
description=$(CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" discover)
grep -Fqx 'status=ready' <<< "$description" || fail 'prepared discovery did not report readiness'
grep -Fqx "root=$prepared" <<< "$description" || fail 'prepared discovery reported wrong root'
environment=$(CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" env)
for path in "$runtime_sysroot/lib/ld-linux-x86-64.so.2" "$runtime_root/lib/libstdc++.so.6" \
    "$runtime_root/bin/cc1" "$runtime_root/bin/cc1plus" "$runtime_sysroot/usr/lib"; do
  mv "$path" "$path.missing"
  for mode in discover env ensure; do
    if CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" "$mode" \
        > "$signal_root/$mode.out" 2> "$signal_root/$mode.err"; then
      fail "$mode accepted a missing borrowed runtime input: $path"
    fi
    [[ ! -s "$signal_root/$mode.out" ]] || fail "$mode exported invalid runtime settings"
  done
  [[ ! -e "$path" && ! -e "$CPKT_TEST_DOWNLOADER_MARKER" && ! -e "$runtime_stage" ]] || fail 'runtime validation generated or repaired state'
  mv "$path.missing" "$path"
done
foreign="$signal_cache/roots/aflplusplus-5.02c-x86_64-linux-gnu-bootlin"
mkdir -p "$foreign"
printf 'another resolver revision\n' > "$foreign/owned-by-other-resolver"
CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" ensure
[[ $(cat "$foreign/owned-by-other-resolver") = 'another resolver revision' ]] || fail 'ensure replaced another resolver revision'
[[ ! -e "$CPKT_TEST_DOWNLOADER_MARKER" ]] || fail 'ensure rebuilt a prepared revision'
rm -f "$CPKT_TEST_BOOTLIN_CALLS"
(
  eval "$environment"
  [[ "$CC" == "$prepared/bin/cpkt-afl-gcc" && "$CXX" == "$prepared/bin/cpkt-afl-g++" &&
     "$AFL_CC" == "$prepared/bin/bootlin-gcc" && "$AFL_CXX" == "$prepared/bin/bootlin-g++" &&
     "$AFL_PATH" == "$prepared/lib/afl" && "$PATH" == "$prepared/bin:"* ]] || fail 'prepared environment selected incorrect tools'
)
for tool in afl-showmap afl-gcc-fast afl-g++-fast afl-cc bootlin-gcc bootlin-g++ ../libexec/bootlin-runtime/cc1 ../libexec/bootlin-runtime/cc1plus; do
  mv "$prepared/bin/$tool" "$prepared/bin/$tool.missing"
  for mode in discover env; do
    if CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" "$mode" > /dev/null 2>&1; then
      fail "$mode accepted incomplete AFL++ without $tool"
    fi
  done
  [[ ! -e "$prepared/bin/$tool" ]] || fail "$mode repaired missing $tool"
  mv "$prepared/bin/$tool.missing" "$prepared/bin/$tool"
done
for path in bin/{afl-fuzz,afl-showmap,cpkt-afl-gcc,cpkt-afl-g++,afl-cc,afl-gcc-fast,afl-g++-fast,bootlin-gcc,bootlin-g++} \
    libexec/bootlin-runtime/{cc1,cc1plus} lib/afl/{afl-gcc-pass.so,afl-compiler-rt.o} .cpkt-aflpp-revision-2-bootlin; do
  mv "$prepared/$path" "$prepared/$path.saved"
  ln -s "$signal_bin/cc" "$prepared/$path"
  for mode in discover env; do
    if CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" "$mode" \
        > "$signal_root/$mode.out" 2> "$signal_root/$mode.err"; then
      fail "$mode accepted an escaping prepared file: $path"
    fi
    [[ ! -s "$signal_root/$mode.out" ]] || fail "$mode published invalid settings"
    grep -Fq 'cpkt-aflpp.sh ensure' "$signal_root/$mode.err" || fail 'invalid root omitted preparation diagnostic'
  done
  [[ -L "$prepared/$path" ]] || fail 'discovery repaired an escaping file'
  rm "$prepared/$path"
  mv "$prepared/$path.saved" "$prepared/$path"
done
mv "$prepared/.cpkt-aflpp-revision-2-bootlin" "$prepared/.cpkt-aflpp-revision-2-other"
for mode in discover env; do
  if CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" "$mode" > /dev/null 2>&1; then
    fail "$mode accepted a wrong-collection AFL++ marker"
  fi
done
[[ ! -e "$CPKT_TEST_BOOTLIN_CALLS" && ! -e "$CPKT_TEST_DOWNLOADER_MARKER" &&
   ! -e "$prepared/.cpkt-aflpp-revision-2-bootlin" ]] || fail 'discovery/environment repaired prepared state'
rm -rf "$signal_cache"

set +e
PATH="$signal_bin:$PATH" CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" ensure >/dev/null 2>&1
signal_status=$?
set -e
[[ $signal_status -ne 0 ]] || fail 'resolver accepted an interrupted AFL++ download'
[[ -s "$CPKT_TEST_BOOTLIN_CALLS" ]] || fail 'ensure did not prepare Bootlin'
[[ -s "$CPKT_TEST_DOWNLOADER_MARKER" ]] || fail 'interruption fixture never reached the downloader'
grep -Fqx signalled "$CPKT_TEST_DOWNLOADER_MARKER" || fail 'interruption fixture did not signal the resolver'
[[ -d "$signal_cache/archives" ]] || fail 'interruption fixture did not create its archive directory'
leftovers=$(find "$signal_cache/archives" -maxdepth 1 -name 'AFLplusplus-5.02c.tar.gz.tmp.*' -print) || fail 'unable to inspect interrupted download cleanup'
[[ -z "$leftovers" ]] || fail 'interrupted AFL++ download left a temporary archive in the shared cache'

# Cancel the top-level resolver while its downloader stays alive until signalled.
cancel_provisioning_fixture() {
export CPKT_TEST_BLOCKED_MARKER="$signal_root/blocked-downloader"
rm -f "$CPKT_TEST_BLOCKED_MARKER"
CPKT_TEST_BLOCK_DOWNLOAD=1 PATH="$signal_bin:$PATH" CPKT_TOOLCHAIN_CACHE="$signal_cache" \
  "$@" > "$signal_root/cancel.log" 2>&1 &
owned_resolver=$!
for attempt in {1..100}; do
  [[ ! -s "$CPKT_TEST_BLOCKED_MARKER" ]] || break
  sleep 0.01
done
[[ -s "$CPKT_TEST_BLOCKED_MARKER" ]] || { kill -KILL "$owned_resolver" 2>/dev/null || :; fail 'blocking downloader was not reached'; }
read -r owned_downloader owned_group < "$CPKT_TEST_BLOCKED_MARKER"
kill -TERM "$owned_resolver"
for attempt in {1..40}; do
  kill -0 "$owned_resolver" 2>/dev/null || break
  sleep 0.1
done
if kill -0 "$owned_resolver" 2>/dev/null; then
  kill -KILL -- "-$owned_group" "$owned_downloader" "$owned_resolver" 2>/dev/null || :
  wait "$owned_resolver" 2>/dev/null || :
  fail 'cancellation did not stop owned provisioning within the bound'
fi
if wait "$owned_resolver"; then fail 'cancelled provisioning returned success'; fi
grep -Fxq stopped "$CPKT_TEST_BLOCKED_MARKER" || fail 'cancellation was not forwarded to downloader'
if kill -0 "$owned_downloader" 2>/dev/null; then fail 'cancelled downloader remains alive'; fi
leftovers=$(find "$signal_cache/archives" -maxdepth 1 -name '*.tmp.*' -print)
[[ -z "$leftovers" ]] || fail 'cancellation cleaned before downloader teardown'

}
cancel_provisioning_fixture "$signal_skill/scripts/cpkt-aflpp.sh" ensure
signal_cache="$signal_root/bootlin-cancel-cache"
cancel_provisioning_fixture "$skill_dir/scripts/cpkt-toolchains.sh" ensure x86_64-linux-gnu

# Exercise runtime wrapper generation without compiling or using a real cache.
source "$skill_dir/scripts/cpkt-afl-runtime.sh"
die() { fail "$*"; }
prepare_runtime() {
  cpkt_afl_prepare_runtime "$runtime_stage" "$runtime_root/bin/compiler" "$runtime_root/bin/compiler" \
    "$runtime_sysroot" "$runtime_root"
}
prepare_runtime
[[ "$cpkt_afl_runtime_loader" = "$runtime_sysroot/lib/ld-linux-x86-64.so.2" ]] || fail 'valid loader pathname changed'
"$cpkt_afl_runtime_cc" 'probe with spaces'
grep -Fxq 'probe with spaces' "$CPKT_TEST_RUNTIME_CALLS" || fail 'generated wrapper lost argv boundaries'
mv "$runtime_stage" "$runtime_stage.saved"
rm "$runtime_sysroot/lib/ld-linux-x86-64.so.2"
ln -s "$signal_bin/cc" "$runtime_sysroot/lib/ld-linux-x86-64.so.2"
if (prepare_runtime) > "$signal_root/runtime.err" 2>&1; then fail 'runtime accepted an escaping loader'; fi
grep -Fq 'outside the selected sysroot' "$signal_root/runtime.err" || fail 'loader refusal was not exercised'
[[ ! -e "$runtime_stage" ]] || fail 'invalid loader produced wrappers'
rm "$runtime_sysroot/lib/ld-linux-x86-64.so.2"
ln -s loader.real "$runtime_sysroot/lib/ld-linux-x86-64.so.2"
mv "$runtime_sysroot/usr/lib" "$runtime_sysroot/usr/lib.saved"
ln -s "$signal_bin" "$runtime_sysroot/usr/lib"
if (prepare_runtime) > "$signal_root/runtime.err" 2>&1; then fail 'runtime accepted an escaping search directory'; fi
grep -Fq 'runtime directory is outside' "$signal_root/runtime.err" || fail 'search-directory refusal was not exercised'
[[ ! -e "$runtime_stage" ]] || fail 'invalid search directory produced wrappers'

printf 'AFL++ resolver tests passed\n'
