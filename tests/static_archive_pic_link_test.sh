#!/usr/bin/env bash
set -eu

if [ "$#" -eq 0 ]; then
  printf 'usage: static_archive_pic_link_test.sh <linked-shared-library>...\n' >&2
  exit 2
fi

for library in "$@"; do
  if [ ! -f "$library" ]; then
    printf 'static archive PIC smoke did not build expected shared library: %s\n' "$library" >&2
    exit 1
  fi
done

printf '[test] static archive PIC link smoke passed\n'
