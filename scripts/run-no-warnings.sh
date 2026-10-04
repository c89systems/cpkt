#!/usr/bin/env bash
set -eu
set -o pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

if [ "$#" -lt 2 ]; then
  printf 'usage: run-no-warnings.sh <description> <command> [args...]\n' >&2
  exit 2
fi

description=$1
shift

mkdir -p "$repo_root/build"
work_root=$(mktemp -d "$repo_root/build/cpkt-no-warnings.XXXXXX")
trap 'rm -rf "$work_root"' EXIT
log_file="$work_root/command.log"

if ! "$@" 2>&1 | tee "$log_file"; then
  printf '%s failed\n' "$description" >&2
  cat "$log_file" >&2
  exit 1
fi

if grep -Ei '(^|[[:space:]:])warning:|^CMake (Deprecation )?Warning([[:space:]:]|$)' "$log_file" >/dev/null 2>&1; then
  printf '%s emitted warnings\n' "$description" >&2
  cat "$log_file" >&2
  exit 1
fi
