#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 6 ]]; then
  printf 'usage: %s <source-dir> <cmake> <generator> <make-program> <toolchain-file> <build-type>\n' "$0" >&2
  exit 2
fi

source_dir=$1
cmake_command=$2
generator=$3
make_program=$4
toolchain_file=$5
build_type=$6
mkdir -p "$source_dir/build"
work_dir=$(mktemp -d "$source_dir/build/dependency-reuse.XXXXXXXX")
trap '"$cmake_command" -E remove_directory "$work_dir"' EXIT HUP INT TERM

cd "$source_dir"
"$cmake_command" -S "$source_dir" -B "$work_dir" -G "$generator" \
  -DCMAKE_MAKE_PROGRAM="$make_program" \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain_file" \
  -DCMAKE_BUILD_TYPE="$build_type" \
  -DCPKT_BUILD_DEPENDENCIES=OFF > "$work_dir/configure.log" 2>&1 || {
    cat "$work_dir/configure.log" >&2
    exit 1
  }

if ! grep -Fq 'Generating done' "$work_dir/configure.log"; then
  cat "$work_dir/configure.log" >&2
  printf 'dependency-reuse configure did not generate a build graph\n' >&2
  exit 1
fi
