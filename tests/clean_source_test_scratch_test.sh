#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: %s <source-dir>\n' "$0" >&2
  exit 2
fi

source_dir=$1
mkdir -p "$source_dir/build"
work_root=$(mktemp -d "$source_dir/build/clean-source-test-scratch.XXXXXXXX")
trap 'cmake -E remove_directory "$work_root"' EXIT

reuse_root="$work_root/reuse"
mkdir -p "$reuse_root/tests"
cp "$source_dir/tests/dependency_reuse_configure_test.sh" "$reuse_root/tests/"
cat > "$work_root/mock-cmake" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
if [[ $# -eq 3 && $1 == -E && $2 == remove_directory ]]; then
  exec cmake -E remove_directory "$3"
fi
if [[ $# -eq 10 && $1 == -S && $3 == -B && $5 == -G &&
      $7 == -DCMAKE_MAKE_PROGRAM=* && $8 == -DCMAKE_TOOLCHAIN_FILE=* &&
      $9 == -DCMAKE_BUILD_TYPE=* && ${10} == -DCPKT_BUILD_DEPENDENCIES=OFF ]]; then
  printf 'Generating done\n'
  exit 0
fi
printf 'unexpected CMake invocation: %s\n' "$*" >&2
exit 2
MOCK
chmod +x "$work_root/mock-cmake"
bash "$reuse_root/tests/dependency_reuse_configure_test.sh" \
  "$reuse_root" "$work_root/mock-cmake" Ninja ninja "" Debug
