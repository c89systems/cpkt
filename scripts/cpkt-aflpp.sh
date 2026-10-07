#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/cpkt-archive-cache.sh"
source "$(dirname -- "${BASH_SOURCE[0]}")/cpkt-afl-runtime.sh"

version=5.02c
build_revision=2
archive_name="AFLplusplus-${version}.tar.gz"
archive_sha256=118415843e5d289d63bd6d8f2252c18212978f15ac9e86acbbc75766cd45acde

die() {
  printf 'cpkt-aflpp: %s\n' "$*" >&2
  exit 1
}

install_cleanup_trap() {
  local remove_option=$1 cleanup='status=$?;' path
  shift
  for path in "$@"; do
    printf -v cleanup '%s rm %s -- %q || :;' "$cleanup" "$remove_option" "$path"
  done
  cleanup+=' trap - EXIT HUP INT TERM; exit "$status"'
  trap "$cleanup" EXIT
  trap 'exit 1' HUP INT TERM
}

with_cache_lock() {
  local lock_path=$1 lock_fd
  shift
  command -v flock >/dev/null 2>&1 || die 'flock is required to provision shared AFL++ tooling'
  mkdir -p "$(dirname -- "$lock_path")"
  exec {lock_fd}>"$lock_path"
  flock -w "${CPKT_TOOLCHAIN_LOCK_TIMEOUT:-600}" "$lock_fd" || die "timed out waiting for shared toolchain lock: $lock_path"
  "$@"
  flock -u "$lock_fd"
  eval "exec ${lock_fd}>&-"
}

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
toolchain_resolver="$repo_root/scripts/cpkt-toolchains.sh"

cache_root() {
  printf '%s\n' "${CPKT_TOOLCHAIN_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/toolchains}"
}

bootlin_collection_id() {
  local root=$1 collection_id
  collection_id=$(basename -- "$root")
  [[ "$collection_id" =~ ^[A-Za-z0-9._-]+$ ]] || die "Bootlin collection identity contains unsupported characters: $collection_id"
  printf '%s\n' "$collection_id"
}

afl_root() {
  local collection_id=$1
  printf '%s/roots/aflplusplus-%s-x86_64-linux-gnu-%s-r%s\n' "$(cache_root)" "$version" "$collection_id" "$build_revision"
}

bootlin_value() {
  local key=$1 description=$2
  sed -n "s/^${key}=//p" <<<"$description" | tail -n 1
}

bootlin_description() {
  [[ -x "$toolchain_resolver" ]] || die "Bootlin resolver is missing: $toolchain_resolver"
  local description
  description=$("$toolchain_resolver" discover x86_64-linux-gnu) || die 'unable to inspect pinned Bootlin collection'
  [[ "$(bootlin_value status "$description")" = ready ]] || die "Bootlin collection is not ready; run: $toolchain_resolver ensure x86_64-linux-gnu"
  printf '%s\n' "$description"
}

afl_ready() {
  local root=$1 collection_id=$2
  [[ -x "$root/bin/afl-fuzz" ]] &&
    [[ -x "$root/bin/afl-showmap" ]] &&
    [[ -x "$root/bin/cpkt-afl-gcc" ]] &&
    [[ -x "$root/bin/cpkt-afl-g++" ]] &&
    [[ -x "$root/bin/afl-cc" ]] &&
    [[ -x "$root/bin/afl-gcc-fast" ]] &&
    [[ -x "$root/bin/afl-g++-fast" ]] &&
    [[ -x "$root/bin/bootlin-gcc" ]] &&
    [[ -x "$root/bin/bootlin-g++" ]] &&
    [[ -x "$root/libexec/bootlin-runtime/cc1" ]] &&
    [[ -x "$root/libexec/bootlin-runtime/cc1plus" ]] &&
    [[ -f "$root/.cpkt-aflpp-revision-${build_revision}-${collection_id}" ]] &&
    [[ -f "$root/lib/afl/afl-gcc-pass.so" ]] &&
    [[ -f "$root/lib/afl/afl-compiler-rt.o" ]]
}

download_archive() {
  local archive_root=$1 archive=$2 temporary
  mkdir -p "$archive_root"
  cpkt_restore_cached_archive "$archive" "$archive_sha256"
  [[ ! -f "$archive" ]] || return 0
  temporary="$archive.tmp.$$"
  install_cleanup_trap -f "$temporary"
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 --connect-timeout 20 --output "$temporary" \
      "https://github.com/AFLplusplus/AFLplusplus/archive/refs/tags/v${version}.tar.gz"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$temporary" "https://github.com/AFLplusplus/AFLplusplus/archive/refs/tags/v${version}.tar.gz"
  else
    die 'curl or wget is required to download AFL++'
  fi
  printf '%s  %s\n' "$archive_sha256" "$temporary" | sha256sum -c - >/dev/null || die "checksum mismatch: $archive_name"
  mv "$temporary" "$archive"
  trap - EXIT HUP INT TERM
}

build_afl() {
  local root=$1 source=$2 cc=$3 cxx=$4 bootlin_root=$5 collection_id=$6 temporary=$7 sysroot=$8
  local helper="$root/lib/afl" bootlin_include_flag bootlin_library_flag
  local make_cc make_cxx link_flags compiler_bin
  compiler_bin=$(dirname -- "$cc")
  printf -v bootlin_include_flag '%q' "-I${bootlin_root}/include"
  printf -v bootlin_library_flag '%q' "-L${bootlin_root}/lib"
  rm -rf "$temporary"
  mkdir -p "$temporary/bin" "$temporary/lib/afl"
  cpkt_afl_prepare_runtime "$temporary" "$cc" "$cxx" "$sysroot" "$bootlin_root"
  printf -v link_flags ' %q' "${cpkt_afl_runtime_link_options[@]}"
  printf -v make_cc '%q' "$cpkt_afl_runtime_cc"
  printf -v make_cxx '%q' "$cpkt_afl_runtime_cxx"
  cc=$cpkt_afl_runtime_cc
  cxx=$cpkt_afl_runtime_cxx

  (
    cd "$source"
    make -j1 NO_PYTHON=1 \
      CC="$make_cc" CXX="$make_cxx" \
      LDFLAGS="$link_flags" \
      PREFIX="$temporary" HELPER_PATH="$helper" BIN_PATH="$root/bin" \
      GCCBINDIR="$compiler_bin" \
      afl-fuzz afl-showmap afl-tmin afl-gotcpu afl-analyze afl-cmin

    "$cc" -O3 -funroll-loops -fPIC -Wall -g \
      -I./include -I./instrumentation \
      "-DAFL_PATH=\"$helper\"" "-DBIN_PATH=\"$root/bin\"" \
      '-DLLVM_BINDIR=""' "-DVERSION=\"++${version}\"" '-DLLVM_LIBDIR=""' \
      '-DLLVM_VERSION=""' '-DAFL_CLANG_FLTO=""' '-DAFL_REAL_LD=""' \
      '-DAFL_CLANG_LDPATH=""' '-DAFL_CLANG_FUSELD=""' \
      "-DCLANG_BIN=\"$root/bin/bootlin-gcc\"" "-DCLANGPP_BIN=\"$root/bin/bootlin-g++\"" -DUSE_BINDIR=1 \
      -Wno-unused-function -Wno-deprecated \
      -c src/afl-common.c -o instrumentation/afl-common.o
    "$cc" -O3 -funroll-loops -fPIC -Wall -g \
      -I./include -I./instrumentation \
      "-DAFL_PATH=\"$helper\"" "-DBIN_PATH=\"$root/bin\"" \
      '-DLLVM_BINDIR=""' "-DVERSION=\"++${version}\"" '-DLLVM_LIBDIR=""' \
      '-DLLVM_VERSION=""' '-DAFL_CLANG_FLTO=""' '-DAFL_REAL_LD=""' \
      '-DAFL_CLANG_LDPATH=""' '-DAFL_CLANG_FUSELD=""' \
      "-DCLANG_BIN=\"$root/bin/bootlin-gcc\"" "-DCLANGPP_BIN=\"$root/bin/bootlin-g++\"" -DUSE_BINDIR=1 \
      -Wno-unused-function -Wno-deprecated \
      "-DAFL_INCLUDE_PATH=\"$root/include/afl\"" \
      src/afl-cc.c instrumentation/afl-common.o -o afl-cc \
      "${cpkt_afl_runtime_link_options[@]}" \
      -DLLVM_MINOR=0 -DLLVM_MAJOR=0 -DCFLAGS_OPT=\"\" -lm
    ln -sf afl-cc afl-gcc-fast
    ln -sf afl-cc afl-g++-fast
    make -j1 -f GNUmakefile.gcc_plugin \
      CC="$make_cc" CXX="$make_cxx" \
      PREFIX="$temporary" HELPER_PATH="$helper" BIN_PATH="$root/bin" \
      GCCBINDIR="$compiler_bin" \
      CXXFLAGS="-O3 -g -funroll-loops ${bootlin_include_flag}${link_flags}" \
      LDFLAGS="${bootlin_library_flag}${link_flags}"

    install -m 755 afl-fuzz afl-showmap afl-tmin afl-gotcpu afl-analyze afl-cmin "$temporary/bin/"
    install -m 755 afl-cc "$temporary/bin/"
    ln -sf afl-cc "$temporary/bin/afl-gcc-fast"
    ln -sf afl-cc "$temporary/bin/afl-g++-fast"
    printf '#!/usr/bin/env bash\nexport AFL_PATH=%q\nexport AFL_CC=%q\nexec %q "$@"\n' \
      "$root/lib/afl" "$root/bin/bootlin-gcc" "$root/bin/afl-gcc-fast" > "$temporary/bin/cpkt-afl-gcc"
    printf '#!/usr/bin/env bash\nexport AFL_PATH=%q\nexport AFL_CC=%q\nexport AFL_CXX=%q\nexec %q "$@"\n' \
      "$root/lib/afl" "$root/bin/bootlin-gcc" "$root/bin/bootlin-g++" "$root/bin/afl-g++-fast" > "$temporary/bin/cpkt-afl-g++"
    chmod +x "$temporary/bin/cpkt-afl-gcc" "$temporary/bin/cpkt-afl-g++"
    install -m 755 afl-gcc-pass.so afl-gcc-cmplog-pass.so afl-gcc-cmptrs-pass.so "$temporary/lib/afl/"
    install -m 644 afl-compiler-rt.o "$temporary/lib/afl/"
    install -m 644 dynamic_list.txt "$temporary/lib/afl/"
    AFL_PATH="$temporary/lib/afl" AFL_CC="$cc" "$temporary/bin/afl-gcc-fast" -O0 test-instr.c -o test-instr "${cpkt_afl_runtime_link_options[@]}"
    "$temporary/bin/afl-showmap" -m none -q -o .cpkt-empty-map ./test-instr </dev/null
    printf '1\n' | "$temporary/bin/afl-showmap" -m none -q -o .cpkt-one-map ./test-instr
    cmp -s .cpkt-empty-map .cpkt-one-map && die 'Bootlin GCC AFL++ instrumentation did not record distinct paths'
    rm -f test-instr .cpkt-empty-map .cpkt-one-map
  )

  touch "$temporary/.cpkt-aflpp-revision-${build_revision}-${collection_id}"
  afl_ready "$temporary" "$collection_id" || die "incomplete AFL++ build: $temporary"
  rm -rf "$root"
  mv "$temporary" "$root"
}

require_native_host() {
  [[ "$(uname -s)" = Linux ]] || die 'AFL++ GCC-plugin fuzzing is supported only on native Linux hosts'
  case "$(uname -m)" in
    x86_64|amd64) ;;
    *) die "AFL++ GCC-plugin fuzzing requires an x86_64 Linux host, got: $(uname -m)" ;;
  esac
}

prepared_description() {
  local description bootlin_root collection_id root
  require_native_host
  description=$(bootlin_description) || return "$?"
  bootlin_root=$(bootlin_value root "$description")
  collection_id=$(bootlin_collection_id "$bootlin_root") || return "$?"
  root=$(afl_root "$collection_id") || return "$?"
  afl_ready "$root" "$collection_id" || die "pinned AFL++ collection is unavailable; run: $repo_root/scripts/cpkt-aflpp.sh ensure"
  printf '%s\n' "$description"
}

ensure() {
  local cache root description bootlin_root collection_id
  require_native_host
  [[ -x "$toolchain_resolver" ]] || die "Bootlin resolver is missing: $toolchain_resolver"
  "$toolchain_resolver" ensure x86_64-linux-gnu >/dev/null
  cache=$(cache_root)
  description=$(bootlin_description) || return "$?"
  bootlin_root=$(bootlin_value root "$description")
  collection_id=$(bootlin_collection_id "$bootlin_root") || return "$?"
  root=$(afl_root "$collection_id") || return "$?"
  afl_ready "$root" "$collection_id" && return
  with_cache_lock "$cache/locks/aflplusplus-${version}-x86_64-linux-gnu.lock" ensure_locked "$cache"
}

ensure_locked() {
  local cache=$1 archive_root archive root description cc cxx bootlin_root collection_id extract source temporary sysroot
  archive_root="$cache/archives"
  archive="$archive_root/$archive_name"
  description=$(bootlin_description) || return "$?"
  cc=$(bootlin_value cc "$description")
  cxx=$(bootlin_value cxx "$description")
  bootlin_root=$(bootlin_value root "$description")
  sysroot=$(bootlin_value sysroot "$description")
  collection_id=$(bootlin_collection_id "$bootlin_root") || return "$?"
  root=$(afl_root "$collection_id") || return "$?"
  afl_ready "$root" "$collection_id" && return
  [[ -x "$cc" && -x "$cxx" && -d "$bootlin_root/include" ]] || die 'Bootlin GCC collection is incomplete for AFL++'
  download_archive "$archive_root" "$archive"
  extract="$cache/.aflplusplus-extract.$$"
  temporary="$root.tmp.$$"
  install_cleanup_trap -rf "$extract" "$temporary"
  rm -rf "$extract" "$temporary"
  mkdir -p "$extract"
  tar -xzf "$archive" -C "$extract"
  source="$extract/AFLplusplus-$version"
  [[ -d "$source" ]] || die "unexpected AFL++ archive layout: $archive_name"
  build_afl "$root" "$source" "$cc" "$cxx" "$bootlin_root" "$collection_id" "$temporary" "$sysroot"
  rm -rf "$extract"
  trap - EXIT HUP INT TERM
}

report() {
  local root description bootlin_root collection_id
  description=$(prepared_description) || return "$?"
  bootlin_root=$(bootlin_value root "$description")
  collection_id=$(bootlin_collection_id "$bootlin_root") || return "$?"
  root=$(afl_root "$collection_id") || return "$?"
  printf 'version=%s\ncache=%s\nsource=aflplusplus\nroot=%s\n' "$version" "$(cache_root)" "$root"
  printf 'afl_fuzz=%s\nafl_showmap=%s\ncc=%s\ncxx=%s\nhelper=%s\n' \
    "$root/bin/afl-fuzz" "$root/bin/afl-showmap" "$root/bin/cpkt-afl-gcc" "$root/bin/cpkt-afl-g++" "$root/lib/afl"
}

print_env() {
  local description root bootlin_cc bootlin_cxx bootlin_root collection_id
  description=$(prepared_description) || return "$?"
  bootlin_root=$(bootlin_value root "$description")
  collection_id=$(bootlin_collection_id "$bootlin_root") || return "$?"
  root=$(afl_root "$collection_id") || return "$?"
  bootlin_cc="$root/bin/bootlin-gcc"
  bootlin_cxx="$root/bin/bootlin-g++"
  printf 'export AFL_PATH=%q\n' "$root/lib/afl"
  printf 'export CPKT_AFLPP_ROOT=%q\n' "$root"
  printf 'export AFL_CC=%q\n' "$bootlin_cc"
  printf 'export AFL_CXX=%q\n' "$bootlin_cxx"
  printf 'export CC=%q\n' "$root/bin/cpkt-afl-gcc"
  printf 'export CXX=%q\n' "$root/bin/cpkt-afl-g++"
  printf 'export PATH=%q\n' "$root/bin:$PATH"
}

case "${1:-}" in
  ensure) [[ $# -eq 1 ]] || die 'usage: cpkt-aflpp.sh ensure'; ensure ;;
  discover) [[ $# -eq 1 ]] || die 'usage: cpkt-aflpp.sh discover'; report ;;
  env) [[ $# -eq 1 ]] || die 'usage: cpkt-aflpp.sh env'; print_env ;;
  *) die 'usage: cpkt-aflpp.sh {ensure|discover|env}' ;;
esac
