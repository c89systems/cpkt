#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: linux_toolchain_sysroot_policy_test.sh <source-dir>\n' >&2
  exit 2
fi

source_dir=$1
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
cache_root="$work_dir/toolchains"
toolchain_name=x86-64--glibc--stable-2026.08-1
toolchain_root="$cache_root/roots/$toolchain_name"
sysroot="$toolchain_root/x86_64-buildroot-linux-gnu/sysroot"
prefix=x86_64-linux
mkdir -p "$toolchain_root/bin" "$sysroot/usr/include" "$sysroot/usr/lib" "$toolchain_root/runtime"
: > "$sysroot/usr/include/stdio.h"
: > "$sysroot/usr/lib/libc.so"
: > "$toolchain_root/runtime/libstdc++.a"
: > "$toolchain_root/runtime/libgcc.a"

for tool in ld ar ranlib strip nm objcopy objdump addr2line gdb readelf; do
  printf '#!/bin/sh\nexit 0\n' > "$toolchain_root/bin/$prefix-$tool"
  chmod +x "$toolchain_root/bin/$prefix-$tool"
done
cat > "$toolchain_root/bin/$prefix-gcc" <<EOF
#!/bin/sh
if [ "\${1:-}" = '-print-prog-name=ld' ]; then
  printf '%s\\n' '$toolchain_root/bin/$prefix-ld'
fi
EOF
chmod +x "$toolchain_root/bin/$prefix-gcc"
cat > "$toolchain_root/bin/$prefix-g++" <<EOF
#!/bin/sh
case "\${1:-}" in
  -print-file-name=libstdc++.a) printf '%s\\n' '$toolchain_root/runtime/libstdc++.a' ;;
  -print-file-name=libgcc.a) printf '%s\\n' '$toolchain_root/runtime/libgcc.a' ;;
  *) exit 1 ;;
esac
EOF
chmod +x "$toolchain_root/bin/$prefix-g++"

cmake_script="$work_dir/check.cmake"
cat > "$cmake_script" <<EOF
include("${source_dir}/cmake/toolchains/cpkt_linux_toolchain_common.cmake")
cpkt_configure_bootlin_toolchain(x86_64-linux-gnu)
if(NOT CPKT_TOOLCHAIN_ROOT STREQUAL "${toolchain_root}")
  message(FATAL_ERROR "Bootlin root was not selected: \${CPKT_TOOLCHAIN_ROOT}")
endif()
if(NOT CMAKE_C_COMPILER STREQUAL "${toolchain_root}/bin/${prefix}-gcc")
  message(FATAL_ERROR "host C compiler selected: \${CMAKE_C_COMPILER}")
endif()
if(NOT CMAKE_CXX_COMPILER STREQUAL "${toolchain_root}/bin/${prefix}-g++")
  message(FATAL_ERROR "host C++ compiler selected: \${CMAKE_CXX_COMPILER}")
endif()
if(DEFINED CMAKE_C_COMPILER_TARGET OR DEFINED CMAKE_CXX_COMPILER_TARGET)
  message(FATAL_ERROR "Bootlin GCC must not receive Clang-only compiler target flags")
endif()
if(NOT CMAKE_LINKER STREQUAL "${toolchain_root}/bin/${prefix}-ld")
  message(FATAL_ERROR "Bootlin linker was not selected: \${CMAKE_LINKER}")
endif()
if(NOT CMAKE_SYSROOT STREQUAL "${sysroot}")
  message(FATAL_ERROR "Bootlin sysroot was not selected: \${CMAKE_SYSROOT}")
endif()
if(NOT CPKT_CXX_STDLIB_STATIC_LIBRARY STREQUAL "${toolchain_root}/runtime/libstdc++.a")
  message(FATAL_ERROR "Bootlin static C++ runtime was not selected")
endif()
if(NOT CPKT_CXX_LIBGCC_STATIC_LIBRARY STREQUAL "${toolchain_root}/runtime/libgcc.a")
  message(FATAL_ERROR "Bootlin static GCC runtime was not selected")
endif()
if(NOT CPKT_TOOLCHAIN_COLLECTION_ID STREQUAL "bootlin-x86_64-linux-gnu-${toolchain_name}-x86_64-buildroot-linux-gnu/sysroot")
  message(FATAL_ERROR "Bootlin compiler collection id was not selected: \${CPKT_TOOLCHAIN_COLLECTION_ID}")
endif()
EOF

CPKT_TOOLCHAIN_CACHE="$cache_root" cmake -P "$cmake_script"

if rg -n -S '/usr/bin/(cc|c\+\+)|CPKT_.*_MUSL_PREFIX|CPKT_AUTO_TOOLCHAINS|ensure-toolchain\.sh' \
    "$source_dir/cmake/toolchains" "$source_dir/scripts/package-install-smoke.sh" >/dev/null; then
  printf 'Linux toolchain lifecycle still contains a host or ad-hoc compiler fallback\n' >&2
  exit 1
fi
for target_file in \
  x86_64-linux-gnu.cmake x86_64-linux-musl.cmake \
  aarch64-linux-gnu.cmake aarch64-linux-musl.cmake \
  armhf-linux-gnu.cmake armhf-linux-musl.cmake; do
  grep -Fq 'cpkt_configure_bootlin_toolchain(' "$source_dir/cmake/toolchains/$target_file" || {
    printf 'Linux target toolchain does not require the pinned Bootlin resolver: %s\n' "$target_file" >&2
    exit 1
  }
done

printf '[test] Linux toolchain policy passed\n'
