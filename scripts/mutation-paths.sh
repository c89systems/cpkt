#!/usr/bin/env bash
# Raw ancestry checks for native metadata mutations; no lifecycle discovery.
cpkt_validate_mutation_path() {
  local path=$1 parent
  case "$path" in /*) ;; *) printf 'mutation path must be absolute: %s\n' "$path" >&2; return 2 ;; esac
  case "$path/" in *'/../'*|*'/./'*) printf 'mutation path contains traversal: %s\n' "$path" >&2; return 2 ;; esac
  parent=${path%/}
  while [ -n "$parent" ]; do
    if [ -L "$parent" ]; then
      printf 'mutation path has a symlink ancestor: %s\n' "$parent" >&2
      return 2
    fi
    parent=${parent%/*}
  done
}
