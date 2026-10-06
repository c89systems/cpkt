#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

find_gnu_tar() {
  if [ "${CPKT_GNU_TAR:-}" != "" ]; then
    if "$CPKT_GNU_TAR" --version 2>&1 | grep -F 'GNU tar' >/dev/null 2>&1; then
      printf '%s\n' "$CPKT_GNU_TAR"
      return 0
    fi
    printf 'CPKT_GNU_TAR is not GNU tar: %s\n' "$CPKT_GNU_TAR" >&2
    return 1
  fi

  for candidate_name in gtar tar; do
    candidate_path=$(command -v "$candidate_name" 2>/dev/null || true)
    if [ "$candidate_path" = "" ]; then
      continue
    fi
    if "$candidate_path" --version 2>&1 | grep -F 'GNU tar' >/dev/null 2>&1; then
      printf '%s\n' "$candidate_path"
      return 0
    fi
  done

  printf 'GNU tar is required for source archive verifier failure fixtures\n' >&2
  return 1
}

gnu_tar=$(find_gnu_tar)
mkdir -p "$repo_root/build"
work_root=$(mktemp -d "$repo_root/build/cpkt-source-verify-failure.XXXXXXXXXX")
cleanup() {
  rm -rf "$work_root"
}
trap cleanup EXIT HUP INT TERM

verifier_root="$work_root/verifier"
mkdir -p "$verifier_root/tests"
cp -a "$repo_root/scripts" "$repo_root/cmake" "$verifier_root/"
cp "$repo_root/CMakePresets.json" "$verifier_root/"
cp "$repo_root/tests/privacy_scan.cmake" "$verifier_root/tests/"
verify_archive() (
  for key in ${!CPKT_OPERATION_@}; do unset "$key"; done
  unset GROUP
  bash "$verifier_root/scripts/source-archive-verify.sh" "$@"
)

required_payloads=$(python3 - "$repo_root" <<'REQUIRED'
import sys
from pathlib import Path
root=Path(sys.argv[1]);sys.path.insert(0,str(root/'scripts'))
from cpkt_inventory import load,validate_inputs
print('\n'.join(validate_inputs(root,load(root))))
REQUIRED
)


make_archive() {
  fixture_name=$1
  shift
  archive_path="$work_root/$fixture_name/cpkt-1.2.3.tar.gz"
  mkdir -p "$(dirname -- "$archive_path")"
  (
    cd "$work_root/$fixture_name/stage"
    "$gnu_tar" --sort=name --owner=0 --group=0 --numeric-owner -czf "$archive_path" -- "$@"
  )
  printf '%s\n' "$archive_path"
}

make_non_root_archive() {
  fixture_name=$1
  shift
  archive_path="$work_root/$fixture_name/cpkt-1.2.3.tar.gz"
  mkdir -p "$(dirname -- "$archive_path")"
  (
    cd "$work_root/$fixture_name/stage"
    "$gnu_tar" --sort=name --owner=1 --group=1 --numeric-owner -czf "$archive_path" -- "$@"
  )
  printf '%s\n' "$archive_path"
}

expect_verify_failure() {
  archive_path=$1
  expected_message=$2
  stderr_path="$work_root/$(basename -- "$archive_path").stderr"

  if verify_archive "$archive_path" 1.2.3 >"$work_root/verify.stdout" 2>"$stderr_path"; then
    printf 'source archive verifier accepted malformed archive: %s\n' "$archive_path" >&2
    exit 1
  fi
  if ! grep -F -- "$expected_message" "$stderr_path" >/dev/null 2>&1; then
    printf 'source archive verifier failure did not include expected message: %s\n' "$expected_message" >&2
    printf 'actual stderr:\n' >&2
    cat "$stderr_path" >&2
    exit 1
  fi
}

write_release_version_script() {
  root=$1
  mkdir -p "$root/scripts"
  cp "$repo_root/scripts/release-version.sh" "$root/scripts/release-version.sh"
}

fixture_root="$work_root/root-mismatch/stage/cpkt-wrong"
mkdir -p "$fixture_root"
printf '1.2.3\n' > "$fixture_root/VERSION"
expect_verify_failure "$(make_archive root-mismatch cpkt-wrong)" "source archive root is cpkt-wrong"

mkdir -p "$work_root/multiple-roots/stage/cpkt-1.2.3" "$work_root/multiple-roots/stage/extra-root"
expect_verify_failure "$(make_archive multiple-roots cpkt-1.2.3 extra-root)" \
  "source archive must contain exactly one root directory"

mkdir -p "$work_root/outside-file/stage/cpkt-1.2.3"
printf 'extra\n' > "$work_root/outside-file/stage/extra.txt"
expect_verify_failure "$(make_archive outside-file cpkt-1.2.3 extra.txt)" \
  "source archive contains entry outside its root"

fixture_root="$work_root/non-root-owner/stage/cpkt-1.2.3"
mkdir -p "$fixture_root"
printf '1.2.3\n' > "$fixture_root/VERSION"
expect_verify_failure "$(make_non_root_archive non-root-owner cpkt-1.2.3)" \
  "source archive entries must be owned by 0/0"

fixture_root="$work_root/missing-version/stage/cpkt-1.2.3"
mkdir -p "$fixture_root"
expect_verify_failure "$(make_archive missing-version cpkt-1.2.3)" \
  "source archive is missing VERSION"

fixture_root="$work_root/version-mismatch/stage/cpkt-1.2.3"
mkdir -p "$fixture_root"
printf '9.9.9\n' > "$fixture_root/VERSION"
expect_verify_failure "$(make_archive version-mismatch cpkt-1.2.3)" \
  "source archive VERSION 9.9.9 does not match expected 1.2.3"

fixture_root="$work_root/missing-manifest/stage/cpkt-1.2.3"
mkdir -p "$fixture_root"
printf '1.2.3\n' > "$fixture_root/VERSION"
write_release_version_script "$fixture_root"
expect_verify_failure "$(make_archive missing-manifest cpkt-1.2.3)" \
  "source archive is missing RELEASE_MANIFEST"

fixture_root="$work_root/forbidden-dist/stage/cpkt-1.2.3"
mkdir -p "$fixture_root/dist"
printf '1.2.3\n' > "$fixture_root/VERSION"
write_release_version_script "$fixture_root"
printf 'VERSION\nRELEASE_MANIFEST\nscripts/release-version.sh\n' > "$fixture_root/RELEASE_MANIFEST"
printf 'stale\n' > "$fixture_root/dist/stale.txt"
expect_verify_failure "$(make_archive forbidden-dist cpkt-1.2.3)" \
  "source archive includes generated/private path: dist"

fixture_root="$work_root/forbidden-privacy-scan/stage/cpkt-1.2.3"
mkdir -p "$fixture_root/privacy-scan-fixture"
printf '1.2.3\n' > "$fixture_root/VERSION"
write_release_version_script "$fixture_root"
printf 'VERSION\nRELEASE_MANIFEST\nscripts/release-version.sh\n' > "$fixture_root/RELEASE_MANIFEST"
expect_verify_failure "$(make_archive forbidden-privacy-scan cpkt-1.2.3)" \
  "source archive includes generated/private path: privacy-scan-fixture"

fixture_root="$work_root/manifest-mismatch/stage/cpkt-1.2.3"
mkdir -p "$fixture_root"
printf '1.2.3\n' > "$fixture_root/VERSION"
while IFS= read -r required; do
  if [ "$required" = "" ]; then
    continue
  fi
  mkdir -p "$fixture_root/$(dirname -- "$required")"
  cp "$repo_root/$required" "$fixture_root/$required"
  printf '%s\n' "$required" >> "$fixture_root/RELEASE_MANIFEST"
done <<EOF
$required_payloads
EOF
printf 'VERSION\nRELEASE_MANIFEST\n' >> "$fixture_root/RELEASE_MANIFEST"
# Build a complete current source fixture, including inventory-followed helpers.
# The malformed fixtures above remain independent of the production packager.
while IFS= read -r payload; do
  case "$payload" in build/*|dist/*|.cache/*|.git/*) continue ;; esac
  [[ -f "$repo_root/$payload" ]] || continue
  mkdir -p "$fixture_root/$(dirname -- "$payload")"
  cp "$repo_root/$payload" "$fixture_root/$payload"
done < <(git -C "$repo_root" ls-files --cached --others --exclude-standard)
(cd "$fixture_root" && find . -type f | sed 's#^\./##' | sed '/^extra-unlisted.txt$/d' | sort > RELEASE_MANIFEST)
printf 'extra\n' > "$fixture_root/extra-unlisted.txt"
expect_verify_failure "$(make_archive manifest-mismatch cpkt-1.2.3)" \
  "source archive payload does not match RELEASE_MANIFEST"

# A parent-only signal during configure must stop before build, not merely
# remove the extraction directory and continue into the next release command.
rm "$fixture_root/extra-unlisted.txt"
(cd "$fixture_root" && find . -type f | sed 's#^\./##' | sort > RELEASE_MANIFEST)
for missing in \
  scripts/package-command.sh \
  tools/generate_cmocka_c89.py \
  tests/sdk-consumers/cpkt_ssl.c
do
  mv "$fixture_root/$missing" "$work_root/held-payload"
  expect_verify_failure "$(make_archive manifest-mismatch cpkt-1.2.3)" \
    "source archive is missing required payload: $missing"
  mv "$work_root/held-payload" "$fixture_root/$missing"
done
valid_archive=$(make_archive manifest-mismatch cpkt-1.2.3)
real_cmake=$(command -v cmake)
mkdir -p "$work_root/bin"
cat > "$work_root/bin/cmake" <<'EOF'
#!/usr/bin/env bash
if [[ $1 == -S ]]; then
  kill -TERM "$PPID"
  exit 0
fi
if [[ $1 == --build ]]; then
  printf 'build ran after interruption\n' > "$CPKT_SIGNAL_BUILD_LOG"
fi
exec "$CPKT_SIGNAL_REAL_CMAKE" "$@"
EOF
chmod +x "$work_root/bin/cmake"
signal_status=0
PATH="$work_root/bin:$PATH" CPKT_SIGNAL_REAL_CMAKE="$real_cmake" \
  CPKT_SOURCE_ARCHIVE_TOOLCHAIN_FILE="$verifier_root/cmake/toolchains/x86_64-linux-gnu.cmake" \
  CPKT_SIGNAL_BUILD_LOG="$work_root/signal-build.log" \
  verify_archive "$valid_archive" 1.2.3 \
    > "$work_root/signal.log" 2>&1 || signal_status=$?
if [[ $signal_status != 143 || -e "$work_root/signal-build.log" ]]; then
  printf 'source verifier continued after SIGTERM (status=%s)\n' "$signal_status" >&2
  cat "$work_root/signal.log" >&2
  exit 1
fi

printf '[test] source archive verifier failure modes passed\n'
