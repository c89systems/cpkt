#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  printf 'usage: %s <smoke|standard|long> <target> <seed-dir>\n' "$0" >&2
  exit 2
fi

mode=$1
target=$2
seed_dir=$3
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
bash "$repo_root/scripts/require-native-hardening-host.sh" afl++

case "$mode" in
  smoke) duration=2 ;;
  standard) duration=30 ;;
  long) duration=${CPKT_AFLPP_LONG_DURATION_SECONDS:-300} ;;
  *) printf 'unknown AFL++ duration mode: %s\n' "$mode" >&2; exit 2 ;;
esac

case "$duration" in
  ''|*[!0-9]*|0)
    printf 'AFL++ duration must be a positive integer number of seconds: %s\n' "$duration" >&2
    exit 2
    ;;
esac

[ -x "$target" ] || { printf 'AFL++ target is not executable: %s\n' "$target" >&2; exit 1; }
[ -d "$seed_dir" ] || { printf 'AFL++ seed directory is missing: %s\n' "$seed_dir" >&2; exit 1; }

eval "$("$repo_root/scripts/cpkt-aflpp.sh" env)"
output_dir=$(mktemp -d "${target}.afl-output.XXXXXX")
cleanup_output_dir() {
  local status=$?
  if [ "$status" -eq 0 ]; then
    rm -rf "$output_dir"
  else
    printf 'AFL++ findings retained in: %s\n' "$output_dir" >&2
  fi
}
trap cleanup_output_dir EXIT
trap 'exit 1' HUP INT TERM
AFL_SKIP_CPUFREQ=1 AFL_NO_AFFINITY=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 \
  "$CPKT_AFLPP_ROOT/bin/afl-fuzz" -V "$duration" -i "$seed_dir" -o "$output_dir" -- "$target" @@

check_finding_dir() {
  local kind=$1
  local description=$2
  local finding_file

  if finding_file=$(find "$output_dir" -type f -path "*/$kind/id:*" -print -quit); then
    if [ -n "$finding_file" ]; then
      printf 'AFL++ recorded %s: %s\n' "$description" "$finding_file" >&2
      exit 1
    fi
  else
    printf 'failed to inspect AFL++ %s findings: %s\n' "$kind" "$output_dir" >&2
    exit 1
  fi
}

check_finding_dir crashes 'a crashing input'
check_finding_dir hangs 'a hanging input'
