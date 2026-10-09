#!/usr/bin/env bash
set -euo pipefail
[[ $# -ge 2 ]] || { echo 'usage: local_executable_runtime_policy_test.sh <loader> <executable>...' >&2; exit 2; }
loader=$1
shift
for exe in "$@"; do
  [[ -x $exe ]] || { printf 'missing local executable: %s\n' "$exe" >&2; exit 1; }
  headers=$(LC_ALL=C readelf -hl "$exe")
  dynamic=$(LC_ALL=C readelf -d "$exe")
  interpreter=$(sed -n 's/.*Requesting program interpreter: \(.*\)]/\1/p' <<< "$headers")
  if [[ -z $interpreter ]]; then
    # A DSO or relocatable object is not a fully static executable.
    if ! grep -Eq 'Type: +(EXEC|DYN)' <<< "$headers" ||
        grep -Eq 'Entry point address: +0x0$' <<< "$headers" ||
        grep -q '(NEEDED)' <<< "$dynamic"; then
      printf 'not a static executable: %s\n' "$exe" >&2
      exit 1
    fi
    continue
  fi
  [[ $interpreter == "$loader" ]] || {
    printf 'local executable uses the wrong loader: %s: %s\n' "$exe" "$interpreter" >&2
    exit 1
  }
  rpath=$(sed -n 's/.*(RPATH).*\[\(.*\)\]/\1/p' <<< "$dynamic")
  case ":$rpath:" in
    *":$(dirname "$loader"):"*) ;;
    *) printf 'selected runtime missing from executable RPATH: %s\n' "$exe" >&2; exit 1 ;;
  esac
done
printf 'all %s local executable targets use the selected runtime or are static\n' "$#"
