#!/usr/bin/env bash
set -euo pipefail

runner=()
qemu=
sysroot=
lua_file=
while [[ $# -gt 0 ]]; do
  case $1 in
    --lua-file) lua_file=${2:?missing Lua fixture}; shift 2 ;;
    --qemu) qemu=${2:?missing QEMU executable}; shift 2 ;;
    --sysroot) sysroot=${2:?missing target sysroot}; shift 2 ;;
    --) shift; break ;;
    *) printf 'unexpected package consumer option: %s\n' "$1" >&2; exit 2 ;;
  esac
done
[[ $# -gt 0 ]] || { printf 'no package consumers supplied\n' >&2; exit 2; }
if [[ -n $qemu || -n $sysroot ]]; then
  [[ -n $qemu && -n $sysroot ]] || {
    printf 'package consumer QEMU execution requires both --qemu and --sysroot\n' >&2
    exit 2
  }
  runner=("$qemu" -L "$sysroot")
fi

for consumer in "$@"; do
  [[ -f $consumer && -x $consumer ]] || {
    printf 'missing executable package consumer: %s\n' "$consumer" >&2
    exit 1
  }
  printf 'Running package consumer: %s\n' "${consumer##*/}"
  arguments=()
  environment=()
  case ${consumer##*/} in
    cpkt_cmake_lua_runtime_strict) arguments=("${lua_file:?missing Lua fixture}") ;;
    cpkt_cmake_sasl_facade_shared) environment=(env -u SASL_PATH) ;;
    *sasl*) environment=(env SASL_PATH=/cpkt-no-external-sasl-plugins) ;;
  esac
  "${environment[@]}" "${runner[@]}" "$consumer" "${arguments[@]}"
done
