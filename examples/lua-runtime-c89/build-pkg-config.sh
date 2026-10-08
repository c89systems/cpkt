#!/usr/bin/env bash
set -euo pipefail
prefix=${CPKT_SDK_PREFIX:?CPKT_SDK_PREFIX must point at an extracted SDK}
output=${1:-./cpkt_lua_runtime_c89_example}
[ "$#" -eq 0 ] || shift
source_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
mkdir -p "$(dirname -- "$output")"
output_dir=$(CDPATH= cd -- "$(dirname -- "$output")" && pwd)
name=$(basename -- "$output")
graph="$output_dir/.cpkt-lua-$name"
arguments=("-DCPKT_SDK_PREFIX=$prefix" "-DCMAKE_C_COMPILER=${CC:-cc}"
  "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$output_dir" "-DCPKT_EXAMPLE_OUTPUT_NAME=$name")
index=0
for flag in "$@"; do
  arguments+=("-DCPKT_EXAMPLE_LINK_FLAG_$index=$flag")
  index=$((index+1))
done
cmake -S "$source_dir/pkg-config" -B "$graph" "${arguments[@]}" "-DCPKT_EXAMPLE_LINK_FLAG_COUNT=$index"
cmake --build "$graph" --target cpkt_lua_runtime_pkg_config_example
