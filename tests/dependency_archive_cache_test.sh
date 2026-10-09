#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: dependency_archive_cache_test.sh <source-dir>\n' >&2
  exit 2
fi

source_dir=$1
mkdir -p "$source_dir/build"
work_dir=$(mktemp -d "$source_dir/build/cpkt-dependency-archive-cache.XXXXXXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cmake -DCPKT_SOURCE_DIR="$source_dir" -DCPKT_TEST_ROOT="$work_dir" \
  -P "$source_dir/tests/dependency_archive_cache_test.cmake"

fixture_hash=$(sha256sum "$work_dir/fixture.tar.gz" | awk '{print $1}')
cmake \
  -S "$source_dir/tests/dependency_archive_external_project_fixture" \
  -B "$work_dir/external-project-build" \
  -DCPKT_SOURCE_DIR="$source_dir" \
  -DCPKT_DEPENDENCY_CACHE="$work_dir/shared/deps" \
  -DCPKT_TEST_URL="file://$work_dir/fixture.tar.gz" \
  -DCPKT_TEST_SHA256="$fixture_hash"
if ! grep -R -F "$work_dir/shared/deps/archives/sha256/$fixture_hash/fixture.tar.gz" \
    "$work_dir/external-project-build" >/dev/null 2>&1; then
  printf 'ExternalProject did not receive the verified shared archive path\n' >&2
  exit 1
fi
cmake --build "$work_dir/external-project-build" --target fixture_source
[[ -f "$work_dir/external-project-build/source/fixture-payload.txt" ]] || {
  printf 'ExternalProject did not extract the verified shared archive\n' >&2
  exit 1
}

set +e
failure_output=$(cmake -DCPKT_SOURCE_DIR="$source_dir" -DCPKT_TEST_ROOT="$work_dir" \
  -P "$source_dir/tests/dependency_archive_cache_failure_test.cmake" 2>&1)
failure_status=$?
set -e
[[ $failure_status -ne 0 ]] || {
  printf 'dependency cache acquisition unexpectedly accepted an unavailable archive\n' >&2
  exit 1
}
[[ "$failure_output" == *'dependency-acquisition: failed to acquire unreachable.tar.gz'* ]] || {
  printf 'dependency cache acquisition did not report the unavailable archive\n%s\n' "$failure_output" >&2
  exit 1
}
[[ "$failure_output" == *'attempt=1'* && "$failure_output" == *'attempt=2'* ]] || {
  printf 'dependency cache acquisition did not report bounded retry attempts\n%s\n' "$failure_output" >&2
  exit 1
}
if find "$work_dir/failed/shared/deps/archives" -name '.*.part-*' -print -quit | grep -q .; then
  printf 'failed dependency acquisition left a temporary shared-cache archive\n' >&2
  exit 1
fi

clean_fixture="$work_dir/clean-fixture"
shared_cache="$work_dir/shared/deps"
mkdir -p "$clean_fixture/scripts" "$clean_fixture/build" "$clean_fixture/.cache" "$clean_fixture/dist" "$shared_cache"
cp -a "$source_dir/scripts" "$clean_fixture/"
cp -a "$source_dir/cmake" "$clean_fixture/"
cp "$source_dir/CMakePresets.json" "$clean_fixture/"
unset GROUP
: > "$shared_cache/verified-archive"
CPKT_DEPENDENCY_CACHE="$shared_cache" "$clean_fixture/scripts/clean.sh"
[[ -f "$shared_cache/verified-archive" ]] || {
  printf 'make clean removed the shared dependency archive cache\n' >&2
  exit 1
}

printf '[test] dependency archive cache passed\n'
