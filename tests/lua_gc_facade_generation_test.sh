#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: lua_gc_facade_generation_test.sh <generated-source>\n' >&2
  exit 2
fi

source_file=$1
if [[ ! -f "$source_file" ]]; then
  printf 'generated Lua facade source does not exist: %s\n' "$source_file" >&2
  exit 1
fi

if ! grep -F 'size_t step_size;' "$source_file" >/dev/null; then
  printf 'Lua GC facade does not retain Lua 5.5 size_t step values\n' >&2
  exit 1
fi
if ! grep -F 'step_size = va_arg(arguments, size_t);' "$source_file" >/dev/null ||
    ! grep -F 'return lua_gc((lua_State *)state, option, step_size);' \
      "$source_file" >/dev/null; then
  printf 'Lua GC step facade does not forward size_t exactly\n' >&2
  exit 1
fi

option_block() {
  awk -v option="$1" '
    $0 == "  if (option == " option ") {" { found = 1 }
    found { print }
    found && $0 == "  }" { exit }
  ' "$source_file"
}

for option in LUA_GCGEN LUA_GCINC; do
  block=$(option_block "$option")
  if [[ "$block" != *'return lua_gc((lua_State *)state, option);'* ||
        "$block" == *'va_arg('* ]]; then
    printf 'Lua %s facade must not read mode-switch arguments\n' "$option" >&2
    exit 1
  fi
done
