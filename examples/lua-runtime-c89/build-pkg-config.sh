#!/usr/bin/env sh
set -eu

if [ "${CPKT_SDK_PREFIX:-}" = "" ]; then
  printf 'CPKT_SDK_PREFIX is required and must point at an extracted cpkt SDK\n' >&2
  exit 2
fi

cc=${CC:-cc}
pkg_config=${PKG_CONFIG:-pkg-config}
output=${1:-./cpkt_lua_runtime_c89_example}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output_dir=$(dirname -- "$output")
main_object="$output_dir/cpkt_lua_runtime_c89_main.o"
module_object="$output_dir/cpkt_lua_runtime_c89_host_module.o"

mkdir -p "$output_dir"

pkg_config_cflags=$(
  PKG_CONFIG_PATH= \
  PKG_CONFIG_LIBDIR="$CPKT_SDK_PREFIX/lib/pkgconfig" \
    "$pkg_config" --cflags cpkt-lua-runtime
)
pkg_config_libs=$(
  PKG_CONFIG_PATH= \
  PKG_CONFIG_LIBDIR="$CPKT_SDK_PREFIX/lib/pkgconfig" \
    "$pkg_config" --static --libs cpkt-lua-runtime
)

"$cc" ${CPKT_EXAMPLE_CFLAGS:-} -std=c89 -Wall -Wextra -Wpedantic \
  -c "$script_dir/main.c" -o "$main_object" \
  $pkg_config_cflags

"$cc" ${CPKT_EXAMPLE_CFLAGS:-} -std=c99 -Wall -Wextra -Wpedantic \
  -c "$script_dir/host_module.c" -o "$module_object" \
  $pkg_config_cflags

"$cc" "$main_object" "$module_object" -o "$output" \
  $pkg_config_libs \
  ${CPKT_EXAMPLE_LDFLAGS:-}
