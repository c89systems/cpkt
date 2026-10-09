#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: osxcross_linker_cache_refresh_test.sh <source-dir>\n' >&2
  exit 2
fi

source_dir=$1
toolchain_file="$source_dir/cmake/toolchains/arm64-apple-darwin.cmake"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/cpkt-osxcross-cache-refresh.XXXXXXXXXX")
cleanup() {
  rm -rf "$work_dir"
}
trap cleanup EXIT HUP INT TERM

make_fake_osxcross() {
  local root=$1 tool
  mkdir -p "$root/bin" "$root/SDK/MacOSX99.sdk/usr/include"
  for tool in clang clang++ ar ranlib ld install_name_tool nm otool strip; do
    : > "$root/bin/arm64-apple-darwin25-$tool"
    chmod 755 "$root/bin/arm64-apple-darwin25-$tool"
  done
}

make_fake_host_mig() {
  local root=$1
  mkdir -p "$root/bin" "$root/libexec"
  : > "$root/bin/mig"
  : > "$root/bin/mig-upstream"
  : > "$root/libexec/migcom"
  : > "$root/TOOLCHAIN"
  chmod 755 "$root/bin/mig" "$root/bin/mig-upstream" "$root/libexec/migcom"
}

old_root="$work_dir/old-osxcross"
new_root="$work_dir/new-osxcross"
cache_root="$work_dir/cache"
host_mig_revision=88753c478c97b9a08bcdb66cecc68ba5881ff3af
make_fake_osxcross "$old_root"
make_fake_osxcross "$new_root"
make_fake_host_mig "$cache_root/roots/host-mig-puredarwin-${host_mig_revision}-x86_64-linux-gnu"

cmake_script="$work_dir/check.cmake"
cat > "$cmake_script" <<EOF
set(ENV{OSXCROSS_ROOT} "$new_root")
set(ENV{CPKT_TOOLCHAIN_CACHE} "$cache_root")
set(CMAKE_LINKER "$old_root/bin/arm64-apple-darwin25-ld" CACHE FILEPATH "")
set(CMAKE_EXE_LINKER_FLAGS "--ld-path=$old_root/bin/arm64-apple-darwin25-ld -Wl,-dead_strip" CACHE STRING "")
set(CMAKE_SHARED_LINKER_FLAGS "-Wl,-headerpad_max_install_names --ld-path=$old_root/bin/arm64-apple-darwin25-ld" CACHE STRING "")
set(CMAKE_MODULE_LINKER_FLAGS "-fuse-ld=$old_root/bin/arm64-apple-darwin25-ld -Wl,-why_load" CACHE STRING "")
include("$toolchain_file")
foreach(platform_variable CPKT_OSXCROSS_HOST CPKT_MACOS_DEPLOYMENT_TARGET)
  if(NOT platform_variable IN_LIST CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)
    message(FATAL_ERROR "Darwin try_compile does not propagate \${platform_variable}")
  endif()
endforeach()
foreach(linker_flags CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
  if(NOT "\${\${linker_flags}}" MATCHES "^--ld-path=$new_root/bin/arm64-apple-darwin25-ld( |$)")
    message(FATAL_ERROR "\${linker_flags} did not refresh to the current Darwin linker: \${\${linker_flags}}")
  endif()
  if("\${\${linker_flags}}" MATCHES "$old_root|--ld-path=.*/old-osxcross|-fuse-ld=")
    message(FATAL_ERROR "\${linker_flags} retained a stale Darwin linker route: \${\${linker_flags}}")
  endif()
endforeach()
if(NOT CMAKE_LINKER STREQUAL "$new_root/bin/arm64-apple-darwin25-ld")
  message(FATAL_ERROR "CMAKE_LINKER did not refresh to the current osxcross root: \${CMAKE_LINKER}")
endif()
EOF

cmake -P "$cmake_script"

printf '[test] osxcross linker cache refresh passed\n'
