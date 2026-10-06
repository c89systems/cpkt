#!/usr/bin/env bash
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
case ${GROUP:-all} in all) ;; *) printf 'this operation requires GROUP=all\n' >&2; exit 2 ;; esac
if [[ -z ${CPKT_OPERATION_FD:-} ]]; then
  exec bash "$repo_root/scripts/operation.sh" --group all -- bash "$0" "$@"
fi
bash "$repo_root/scripts/operation.sh" --group all --check

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  printf 'usage: %s <archive.tar.gz> [expected-version]\n' "$0" >&2
  exit 2
fi

archive_path=$1
expected_version=${2:-}
case "$archive_path" in
  /*) ;;
  *)
    archive_dir=$(CDPATH= cd -- "$(dirname -- "$archive_path")" && pwd)
    archive_path="$archive_dir/$(basename -- "$archive_path")"
    ;;
esac

case "$archive_path" in
  *.tar.gz) ;;
  *)
    printf 'source archive must be a .tar.gz file: %s\n' "$archive_path" >&2
    exit 1
    ;;
esac
if [ ! -f "$archive_path" ]; then
  printf 'source archive does not exist: %s\n' "$archive_path" >&2
  exit 1
fi

archive_name=$(basename -- "$archive_path")
archive_stem=${archive_name%.tar.gz}
case "$archive_stem" in
  cpkt-*) ;;
  *)
    printf 'unexpected source archive name: %s\n' "$archive_name" >&2
    exit 1
    ;;
esac
archive_version=${archive_stem#cpkt-}
if [ -z "$expected_version" ]; then
  expected_version=$archive_version
fi
if [ "$archive_version" != "$expected_version" ]; then
  printf 'source archive version %s does not match expected %s\n' "$archive_version" "$expected_version" >&2
  exit 1
fi

source "$repo_root/scripts/lifecycle-common.sh"
reconstruction_log="$repo_root/build/verification/source/$expected_version/reconstruction.log"
cpkt_owned_path "$reconstruction_log"

if tar --numeric-owner -tvf "$archive_path" | awk '$2 != "0/0" { print; bad = 1 } END { exit bad }'; then
  :
else
  printf 'source archive entries must be owned by 0/0\n' >&2
  exit 1
fi
archive_listing=$(cmake -E tar tf "$archive_path")
while IFS= read -r entry; do
  case "$entry" in
    /*|..|../*|*/../*|*/..)
      printf 'source archive contains unsafe entry: %s\n' "$entry" >&2
      exit 1
      ;;
  esac
done <<< "$archive_listing"

mkdir -p "$repo_root/build"
work_dir=$(mktemp -d "$repo_root/build/cpkt-source-verify.XXXXXXXXXX")
cleanup() {
  rm -rf "$work_dir"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

python3 "$repo_root/scripts/cpkt_archive_extract.py" "$archive_path" "$work_dir" "$archive_stem" --source

root_count=$(find "$work_dir" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')
if [ "$root_count" != "1" ]; then
  printf 'source archive must contain exactly one root directory\n' >&2
  exit 1
fi
source_root="$work_dir/$archive_stem"
if [ ! -d "$source_root" ]; then
  actual_root=
  for candidate in "$work_dir"/*; do
    if [ -d "$candidate" ]; then
      actual_root=$(basename -- "$candidate")
      break
    fi
  done
  printf 'source archive root is %s, expected %s\n' "$actual_root" "$archive_stem" >&2
  exit 1
fi
while IFS= read -r entry; do
  case "$entry" in
    "$archive_stem"|"$archive_stem/"|"$archive_stem/"*) ;;
    *)
      printf 'source archive contains entry outside its root: %s\n' "$entry" >&2
      exit 1
      ;;
  esac
done <<< "$archive_listing"

if [ ! -f "$source_root/VERSION" ]; then
  printf 'source archive is missing VERSION\n' >&2
  exit 1
fi
source_version=$(sed -n '1{s/[[:space:]]*$//;p;q;}' "$source_root/VERSION")
if [ "$source_version" != "$expected_version" ]; then
  printf 'source archive VERSION %s does not match expected %s\n' "$source_version" "$expected_version" >&2
  exit 1
fi

resolved_version=$(bash "$source_root/scripts/release-version.sh" "$source_root")
if [ "$resolved_version" != "$expected_version" ]; then
  printf 'non-git version resolution returned %s, expected %s\n' "$resolved_version" "$expected_version" >&2
  exit 1
fi

if [ ! -f "$source_root/RELEASE_MANIFEST" ]; then
  printf 'source archive is missing RELEASE_MANIFEST\n' >&2
  exit 1
fi

for forbidden in .git .cache build dist; do
  if [ -e "$source_root/$forbidden" ]; then
    printf 'source archive includes generated/private path: %s\n' "$forbidden" >&2
    exit 1
  fi
done

for scratch in "$source_root"/privacy-scan-*; do
  if [ -e "$scratch" ] || [ -L "$scratch" ]; then
    printf 'source archive includes generated/private path: %s\n' "${scratch##*/}" >&2
    exit 1
  fi
done

python3 - "$source_root" <<'CHECK_MANIFEST'
import sys
from pathlib import Path,PurePosixPath
root=Path(sys.argv[1])
for name in (root/'RELEASE_MANIFEST').read_text().splitlines():
    path=PurePosixPath(name)
    if not name or path.is_absolute() or path.as_posix()!=name or '..' in path.parts:
        sys.exit('invalid source archive manifest path: '+name)
    if not (root/name).is_file() or (root/name).is_symlink():
        sys.exit('source archive is missing required payload: '+name)
CHECK_MANIFEST

python3 - "$source_root" <<'CHECK_INVENTORY'
import sys
from pathlib import Path
root=Path(sys.argv[1])
sys.path.insert(0,str(root/'scripts'))
from cpkt_inventory import load,validate_inputs
validate_inputs(root,load(root))
CHECK_INVENTORY


(
  cd "$source_root"
  find . -type f | sed 's#^\./##' | sort > "$work_dir/actual-files.txt"
)
sort "$source_root/RELEASE_MANIFEST" > "$work_dir/manifest-files.txt"
if ! diff -u "$work_dir/manifest-files.txt" "$work_dir/actual-files.txt"; then
  printf 'source archive payload does not match RELEASE_MANIFEST\n' >&2
  exit 1
fi

cmake \
  -DCPKT_ROOT="$repo_root" \
  -DCPKT_SCAN_LABEL="source archive" \
  -DCPKT_SCAN_PATHS="$archive_path" \
  -P "$repo_root/tests/privacy_scan.cmake"


if [[ -n ${CPKT_SOURCE_ARCHIVE_TOOLCHAIN_FILE:-} && ! -f $CPKT_SOURCE_ARCHIVE_TOOLCHAIN_FILE ]]; then
  printf 'source archive toolchain file does not exist: %s\n' "$CPKT_SOURCE_ARCHIVE_TOOLCHAIN_FILE" >&2
  exit 1
fi
if [[ -n ${CPKT_SOURCE_ARCHIVE_TOOLCHAIN_FILE:-} && $(realpath "$CPKT_SOURCE_ARCHIVE_TOOLCHAIN_FILE") != $(realpath "$repo_root/cmake/toolchains/x86_64-linux-gnu.cmake") ]]; then
  printf 'source reconstruction requires the selected pinned native GNU toolchain, not an arbitrary override\n' >&2
  exit 1
fi
source "$repo_root/scripts/source-environment.sh" "$repo_root"
mkdir -p "${reconstruction_log%/*}"
bash "$repo_root/scripts/operation.sh" --root "$repo_root" --group all \
  --source-root "$source_root" -- bash "$source_root/scripts/source-reconstruct.sh" 2>&1 | tee "$reconstruction_log"
python3 "$repo_root/scripts/cpkt_source_proof.py" "$archive_path" "$expected_version" "$source_root"
printf '[package] verified source archive %s\n' "$archive_path"
