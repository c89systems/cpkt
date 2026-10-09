#!/usr/bin/env bash
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

fake_readelf="$work_dir/readelf"
cat > "$fake_readelf" <<'SH'
#!/usr/bin/env sh
case "$2" in
  *valid.so)
    printf 'Dynamic section at offset 0 contains 1 entry:\n'
    printf ' 0x000000000000001d (RUNPATH)            Library runpath: [$ORIGIN]\n'
    ;;
  *absolute.so)
    printf 'Dynamic section at offset 0 contains 1 entry:\n'
    printf ' 0x000000000000001d (RUNPATH)            Library runpath: [$ORIGIN:/tmp/cpkt-leak]\n'
    ;;
  *missing.so)
    printf 'Dynamic section at offset 0 contains 1 entry:\n'
    printf ' 0x000000000000000e (SONAME)             Library soname: [libmissing.so]\n'
    ;;
  *empty.so)
    printf 'Dynamic section at offset 0 contains 1 entry:\n'
    printf ' 0x000000000000001d (RUNPATH)            Library runpath: []\n'
    ;;
  *)
    printf 'unexpected readelf target: %s\n' "$2" >&2
    exit 2
    ;;
esac
SH
chmod +x "$fake_readelf"

touch "$work_dir/valid.so" "$work_dir/absolute.so" "$work_dir/missing.so" "$work_dir/empty.so"

output=$(
  cmake \
    -DCPKT_READELF="$fake_readelf" \
    -DCPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNTIME_METADATA=ON \
    -DCPKT_PACKAGE_ASSERTIONS_TEST_ELF="$work_dir/valid.so" \
    -P "$repo_root/cmake/package_assertions.cmake"
)

case "$output" in
  *"CPKT_TEST_ELF_RUNTIME_METADATA=ok"*) ;;
  *)
    printf 'package assertion runtime-metadata check rejected valid $ORIGIN metadata\n%s\n' "$output" >&2
    exit 1
    ;;
esac

output=$(
  cmake \
    -DCPKT_READELF="$fake_readelf" \
    -DCPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNTIME_METADATA=ON \
    -DCPKT_PACKAGE_ASSERTIONS_TEST_ELF="$work_dir/missing.so" \
    -P "$repo_root/cmake/package_assertions.cmake"
)

case "$output" in
  *"CPKT_TEST_ELF_RUNTIME_METADATA=ok"*) ;;
  *)
    printf 'package assertion runtime-metadata check rejected ELF without runtime metadata\n%s\n' "$output" >&2
    exit 1
    ;;
esac

if output=$(
    cmake \
      -DCPKT_READELF="$fake_readelf" \
      -DCPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNTIME_METADATA=ON \
      -DCPKT_PACKAGE_ASSERTIONS_TEST_ELF="$work_dir/absolute.so" \
      -P "$repo_root/cmake/package_assertions.cmake" 2>&1
  ); then
  printf 'package assertion runtime-metadata check accepted absolute runpath\n%s\n' "$output" >&2
  exit 1
fi

case "$output" in
  *"non-relocatable RUNPATH/RPATH entry:"*"/tmp/cpkt-leak"*) ;;
  *)
    printf 'package assertion runtime-metadata failure was not actionable\n%s\n' "$output" >&2
    exit 1
    ;;
esac

if output=$(
    cmake \
      -DCPKT_READELF="$fake_readelf" \
      -DCPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNTIME_METADATA=ON \
      -DCPKT_PACKAGE_ASSERTIONS_TEST_ELF="$work_dir/empty.so" \
      -P "$repo_root/cmake/package_assertions.cmake" 2>&1
  ); then
  printf 'package assertion runtime-metadata check accepted an empty runpath\n%s\n' "$output" >&2
  exit 1
fi

case "$output" in
  *"has an empty RUNPATH/RPATH entry"*) ;;
  *)
    printf 'package assertion empty-runpath failure was not actionable\n%s\n' "$output" >&2
    exit 1
    ;;
esac

output=$(
  cmake \
    -DCPKT_READELF="$fake_readelf" \
    -DCPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNPATH=ON \
    -DCPKT_PACKAGE_ASSERTIONS_TEST_ELF="$work_dir/valid.so" \
    -DCPKT_PACKAGE_ASSERTIONS_TEST_EXPECTED_RUNPATH='\$ORIGIN' \
    -P "$repo_root/cmake/package_assertions.cmake"
)

case "$output" in
  *"CPKT_TEST_ELF_RUNPATH=ok"*) ;;
  *)
    printf 'package assertion ELF runpath check rejected valid $ORIGIN metadata\n%s\n' "$output" >&2
    exit 1
    ;;
esac

if output=$(
    cmake \
      -DCPKT_READELF="$fake_readelf" \
      -DCPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNPATH=ON \
      -DCPKT_PACKAGE_ASSERTIONS_TEST_ELF="$work_dir/absolute.so" \
      -DCPKT_PACKAGE_ASSERTIONS_TEST_EXPECTED_RUNPATH='\$ORIGIN' \
      -P "$repo_root/cmake/package_assertions.cmake" 2>&1
  ); then
  printf 'package assertion ELF runpath check accepted absolute runpath\n%s\n' "$output" >&2
  exit 1
fi

case "$output" in
  *"non-relocatable RUNPATH/RPATH entry:"*"/tmp/cpkt-leak"*) ;;
  *)
    printf 'package assertion ELF runpath failure was not actionable\n%s\n' "$output" >&2
    exit 1
    ;;
esac

if output=$(
    cmake \
      -DCPKT_READELF="$fake_readelf" \
      -DCPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNPATH=ON \
      -DCPKT_PACKAGE_ASSERTIONS_TEST_ELF="$work_dir/missing.so" \
      -DCPKT_PACKAGE_ASSERTIONS_TEST_EXPECTED_RUNPATH='\$ORIGIN' \
      -P "$repo_root/cmake/package_assertions.cmake" 2>&1
  ); then
  printf 'package assertion ELF runpath check accepted missing runpath\n%s\n' "$output" >&2
  exit 1
fi

case "$output" in
  *"must have RUNPATH/RPATH [\\\$ORIGIN]"*) ;;
  *)
    printf 'package assertion ELF missing-runpath failure was not actionable\n%s\n' "$output" >&2
    exit 1
    ;;
esac

printf '[test] package assertion ELF runpath policy passed\n'
