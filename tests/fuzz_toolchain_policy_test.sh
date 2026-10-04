#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: fuzz_toolchain_policy_test.sh <source-dir>\n' >&2
  exit 2
fi

source_dir=$1
if [[ -z ${CPKT_OPERATION_FD:-} ]]; then
  exec python3 "$source_dir/scripts/cpkt_operation.py" --group all -- bash "$0" "$source_dir"
fi
mkdir -p "$source_dir/build/fixtures"
work_dir=$(mktemp -d "$source_dir/build/fixtures/fuzz-policy.XXXXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

if output=$(
  cmake -S "$source_dir" -B "$work_dir/no-afl-toolchain" -G Ninja \
    "-DCMAKE_TOOLCHAIN_FILE=$source_dir/cmake/toolchains/x86_64-linux-gnu.cmake" \
    -DCPKT_BUILD_DEPENDENCIES=OFF \
    -DCPKT_FACADE_ONLY=ON \
    -DCPKT_ENABLE_FUZZING=ON \
    2>&1
); then
  printf 'CPKT_ENABLE_FUZZING configured without the AFL++ toolchain\n' >&2
  exit 1
fi

case "$output" in
  *'CPKT_ENABLE_FUZZING requires the pinned AFL++ toolchain'*) ;;
  *)
    printf 'missing actionable AFL++ toolchain diagnostic\n%s\n' "$output" >&2
    exit 1
    ;;
esac

fake_afl_root="$work_dir/fake-afl"
mkdir -p "$fake_afl_root/bin" "$fake_afl_root/lib/afl"
touch "$fake_afl_root/bin/afl-fuzz" "$fake_afl_root/lib/afl/afl-gcc-pass.so"

if output=$(
  cmake -S "$source_dir" -B "$work_dir/no-afl-wrappers" -G Ninja \
    "-DCMAKE_TOOLCHAIN_FILE=$source_dir/cmake/toolchains/x86_64-linux-gnu.cmake" \
    -DCPKT_BUILD_DEPENDENCIES=OFF \
    -DCPKT_FACADE_ONLY=ON \
    -DCPKT_ENABLE_FUZZING=ON \
    "-DCPKT_AFLPP_ROOT=$fake_afl_root" \
    2>&1
); then
  printf 'CPKT_ENABLE_FUZZING configured without the AFL++ compiler wrappers\n' >&2
  exit 1
fi

case "$output" in
  *'CPKT_ENABLE_FUZZING requires the pinned AFL++ compiler wrappers'*) ;;
  *)
    printf 'missing actionable AFL++ compiler wrapper diagnostic\n%s\n' "$output" >&2
    exit 1
    ;;
esac
