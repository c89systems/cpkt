#!/usr/bin/env bash
set -euo pipefail
skill_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
repo_root=${1:?pass the repository root for fixture scratch}
[ "$#" -eq 1 ] && [ -f "$repo_root/CMakeLists.txt" ] || exit 2
[ ! -L "$repo_root/build" ] || { printf 'fixture build root must not be a symlink\n' >&2; exit 2; }
grep -Fxq '/build/' "$repo_root/.gitignore" || { printf 'fixture requires ignored /build/\n' >&2; exit 2; }
mkdir -p "$repo_root/build"
work=$(mktemp -d "$repo_root/build/skill-test-reuse.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
source_dir="$work/source"
binary_dir="$work/build"
mkdir -p "$source_dir"
for name in CMakeLists.txt RunSuite.cmake; do
  awk -v heading="## $name" '
    $0 == heading {wanted=1; next}
    wanted && $0 == "```cmake" {code=1; next}
    code && $0 == "```" {exit}
    code {print}
  ' "$skill_dir/references/native-test-reuse.md" > "$source_dir/$name"
  [ -s "$source_dir/$name" ]
done
cat > "$source_dir/Fixture.cmake" <<'FIXTURE'
if(EXISTS "${COUNTER}")
  file(READ "${COUNTER}" runs)
else()
  set(runs 0)
endif()
math(EXPR runs "${runs} + 1")
file(WRITE "${COUNTER}" "${runs}")
file(READ "${INPUT}" value)
if(NOT value STREQUAL "pass\n")
  message(FATAL_ERROR "Fixture failure requested")
endif()
FIXTURE
printf 'pass\n' > "$source_dir/input.txt"
fail() { cat "$work/log" >&2; printf '%s\n' "$*" >&2; exit 1; }
configure() { cmake -S "$source_dir" -B "$binary_dir" "$@" > "$work/log" 2>&1; }
check() { cmake --build "$binary_dir" --target check > "$work/log" 2>&1; }
expect_runs() { [ "$(cat "$binary_dir/runs.txt")" = "$1" ] || fail "expected $1 CTest executions"; }
expect_failure() {
  if check; then fail 'invalid suite unexpectedly passed'; fi
  [ ! -e "$binary_dir/contract.passed" ] || fail 'failure retained success marker'
}
configure
check
expect_runs 1
check
expect_runs 1
touch "$source_dir/input.txt"
check
expect_runs 1
cp -p "$source_dir/input.txt" "$work/original-input"
printf 'fail\n' > "$source_dir/input.txt"
touch -r "$work/original-input" "$source_dir/input.txt"
expect_failure
expect_runs 2
printf 'pass\n' > "$source_dir/input.txt"
check
expect_runs 3
cp -p "$source_dir/Fixture.cmake" "$work/original-fixture"
printf '\n# changed fixture input\n' >> "$source_dir/Fixture.cmake"
touch -r "$work/original-fixture" "$source_dir/Fixture.cmake"
check
expect_runs 4
configure -DCHECK_MODE=alternate
check
expect_runs 5
cmake -E rm -f "$binary_dir/contract.passed"
check
expect_runs 6
mv "$source_dir/input.txt" "$work/saved-input"
expect_failure
expect_runs 6
mv "$work/saved-input" "$source_dir/input.txt"
cp "$source_dir/CMakeLists.txt" "$work/with-case"
sed 's/add_test(NAME contract/add_test(NAME unrelated/' "$source_dir/CMakeLists.txt" > "$work/without-case"
cp "$work/without-case" "$source_dir/CMakeLists.txt"
configure
expect_failure
expect_runs 6
cp "$work/with-case" "$source_dir/CMakeLists.txt"
configure -DCHECK_MODE=normal
check
expect_runs 7
printf '\nmessage("skip requested")\n' >> "$source_dir/Fixture.cmake"
printf '\nset_tests_properties(contract PROPERTIES SKIP_REGULAR_EXPRESSION "skip requested")\n' >> "$source_dir/CMakeLists.txt"
configure
expect_failure
grep -q 'Skipped' "$work/log" || fail 'skip-regex case did not exercise a skipped CTest result'
expect_runs 8
expect_failure
expect_runs 9
cp "$work/original-fixture" "$source_dir/Fixture.cmake"
cp "$work/with-case" "$source_dir/CMakeLists.txt"
configure
check
expect_runs 10
printf '\nset_tests_properties(contract PROPERTIES SKIP_RETURN_CODE 1)\n' >> "$source_dir/CMakeLists.txt"
printf 'fail\n' > "$source_dir/input.txt"
configure
expect_failure
grep -q 'Skipped' "$work/log" || fail 'skip-return-code case did not exercise a skipped CTest result'
expect_runs 11
printf 'pass\n' > "$source_dir/input.txt"
cp "$work/with-case" "$source_dir/CMakeLists.txt"
configure
check
expect_runs 12
printf '<testsuite><testcase name="contract"><skipped/></testcase></testsuite>\n' > "$binary_dir/contract-results.xml"
check
expect_runs 13
check
expect_runs 13
cmake -E rm -f "$binary_dir/contract-results.xml"
check
expect_runs 14
printf '\nmessage("fixture payload: <skipped/>")\n' >> "$source_dir/Fixture.cmake"
check
expect_runs 15
printf 'native CMake/CTest reuse, skipped-result rejection and result integrity fixture passed\n'
