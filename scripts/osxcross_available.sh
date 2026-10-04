#!/usr/bin/env bash
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
report=$("$script_dir/cpkt-toolchains.sh" discover arm64-apple-darwin)
printf '%s\n' "$report" | grep -Fxq 'status=ready' || exit 1
root=$(printf '%s\n' "$report" | sed -n 's/^root=//p')
host=$(printf '%s\n' "$report" | sed -n 's/^prefix=//p')
test -x "$root/bin/$host-install_name_tool" || exit 1
for sdk in "$root"/SDK/MacOSX*.sdk; do
  if [ -d "$sdk/usr/include" ]; then
    exit 0
  fi
done

exit 1
