#!/usr/bin/env bash
set -euo pipefail
[[ $# -eq 2 ]] || { printf 'usage: aflpp_bootlin_runtime_test.sh <source-dir> <Bootlin-root>\n' >&2; exit 2; }
source_dir=$1
configured_root=$2
die() { printf 'AFL++ Bootlin runtime: %s\n' "$*" >&2; exit 1; }
description=$(bash "$source_dir/scripts/cpkt-toolchains.sh" discover x86_64-linux-gnu)
value() { sed -n "s/^$1=//p" <<< "$description"; }
[[ $(value status) == ready ]] || die 'prepare the native Bootlin collection first'
collection=$(value root)
[[ $(readlink -f -- "$collection") == $(readlink -f -- "$configured_root") ]] || die 'configured Bootlin collection differs from discovery'
cc=$(value cc)
cxx=$(value cxx)
sysroot=$(value sysroot)
readelf=$(value readelf)
mkdir -p "$source_dir/build/fixtures"
work=$(mktemp -d "$source_dir/build/fixtures/afl-runtime.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
cmp "$source_dir/scripts/cpkt-afl-runtime.sh" "$source_dir/skills/pkt-systems-cmake-lifecycle/scripts/cpkt-afl-runtime.sh"
source "$source_dir/scripts/cpkt-afl-runtime.sh"
before_environment="${LD_LIBRARY_PATH-}:${LD_PRELOAD-}:${GCC_EXEC_PREFIX-}:${COMPILER_PATH-}"
cpkt_afl_prepare_runtime "$work/staging root" "$cc" "$cxx" "$sysroot" "$collection"
[[ $before_environment == "${LD_LIBRARY_PATH-}:${LD_PRELOAD-}:${GCC_EXEC_PREFIX-}:${COMPILER_PATH-}" ]] || die 'runtime preparation changed host subprocess environment'
mkdir -p "$work/host-tools"
for name in gcc g++ cc c++ clang clang++; do
  printf '#!/bin/sh\n: > %q\nexit 91\n' "$work/host-compiler-called" > "$work/host-tools/$name"
  chmod +x "$work/host-tools/$name"
done
export PATH="$work/host-tools:$PATH"
if (cpkt_afl_prepare_runtime "$work/forbidden" "$work/host-tools/gcc" "$work/host-tools/g++" "$sysroot" "$collection") > "$work/rejection" 2>&1; then
  die 'host compiler was accepted'
fi
[[ ! -e "$work/host-compiler-called" ]] || die 'host compiler ran before rejection'
grep -Fq 'selected Bootlin compiler' "$work/rejection" || die 'host compiler rejection was not actionable'
mv "$work/staging root" "$work/published root"
runtime_cc="$work/published root/bin/bootlin-gcc"
runtime_cxx="$work/published root/bin/bootlin-g++"
plugin_include=$("$runtime_cc" -print-file-name=plugin)/include
cat > "$work/plugin.cc" <<'CPP'
#include <gcc-plugin.h>
#include <gnu/libc-version.h>
#include <stdio.h>
#include <string>
int plugin_is_GPL_compatible;
int plugin_init(struct plugin_name_args *, struct plugin_gcc_version *) {
  std::string version(gnu_get_libc_version());
  fprintf(stderr, "plugin libc=%s\n", version.c_str());
  return 0;
}
CPP
"$runtime_cxx" -std=c++11 -Wall -Wextra -Werror -shared -fPIC -fno-rtti -fno-exceptions \
  -isystem "$plugin_include" -isystem "$collection/include" \
  "${cpkt_afl_runtime_link_options[@]}" "$work/plugin.cc" -o "$work/plugin.so"
cat > "$work/target.c" <<'C'
#include <gnu/libc-version.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
  printf("target libc=%s\n", gnu_get_libc_version());
  fflush(stdout);
  return system("getconf GNU_LIBC_VERSION | sed 's/^glibc /host child libc=/'") == 0 ? 0 : 1;
}
C
host_version=$(getconf GNU_LIBC_VERSION)
host_version=${host_version#glibc }
for mode in c c++; do
  compiler=$runtime_cc
  [[ $mode != c++ ]] || compiler=$runtime_cxx
  "$compiler" -x "$mode" -Wall -Wextra -Werror -fplugin="$work/plugin.so" \
    "$work/target.c" "${cpkt_afl_runtime_link_options[@]}" -o "$work/target" 2> "$work/compiler.log"
  output=$("$work/target")
  version=$(sed -n 's/^target libc=//p' <<< "$output")
  [[ -n $version ]] || die 'compiled target did not report its runtime'
  grep -Fxq "plugin libc=$version" "$work/compiler.log" || die 'GCC plugin and target used different libc versions'
  grep -Fxq "host child libc=$host_version" <<< "$output" || die 'private runtime contaminated a host child'
  for tool in as ld; do
    tool_path=$("$compiler" -print-prog-name="$tool")
    [[ $(readlink -f -- "$tool_path") == "$collection/"* ]] || die "compiler selected a host $tool"
  done
done
headers=$("$readelf" -l "$work/target")
grep -Fq "Requesting program interpreter: $cpkt_afl_runtime_loader]" <<< "$headers" || die 'target uses the host interpreter'
for backend in cc1 cc1plus; do
  path=$("$cc" -print-prog-name="$backend")
  resolution=$("$cpkt_afl_runtime_loader" --library-path "$cpkt_afl_runtime_library_path" --list "$path")
  while IFS= read -r line; do
    [[ $line == *'=> '* ]] || continue
    dependency=${line#*'=> '}
    dependency=${dependency%%" ("*}
    [[ $(readlink -f -- "$dependency") == "$collection/"* ]] || die "backend resolved a host dependency: $line"
  done <<< "$resolution"
done
[[ ! -e "$work/host-compiler-called" ]] || die 'a host compiler fallback ran'
printf '[test] AFL++ compiler backends and targets use Bootlin; host children retain host runtime\n'
