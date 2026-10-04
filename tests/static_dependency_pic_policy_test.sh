#!/usr/bin/env bash
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
deps_file="$repo_root/cmake/CpktDependencies.cmake"

if ! grep -F -- "--with-pic" "$deps_file" >/dev/null 2>&1; then
  printf 'nghttp2 static archive policy must configure autotools with --with-pic\n' >&2
  exit 1
fi

printf '[test] static dependency PIC policy passed\n'
