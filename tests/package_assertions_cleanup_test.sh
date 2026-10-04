#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: package_assertions_cleanup_test.sh <source-dir>\n' >&2
  exit 2
fi

source_dir=$1
assertion_parent="$source_dir/build/package-assertions"
mkdir -p "$source_dir/build/fixtures"
work_dir=$(mktemp -d "$source_dir/build/fixtures/cleanup.XXXXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT

if [[ -d "$assertion_parent" ]]; then
  parent_existed=1
  before=$(find "$assertion_parent" -mindepth 1 -printf '%P\n' | LC_ALL=C sort)
else
  parent_existed=0
  before=
fi
legacy_before=$(find "$source_dir" -mindepth 1 -maxdepth 1 -type d -name 'package-assertions-*' -printf '%f\n' | LC_ALL=C sort)

archive_root='cpkt-0.0.0-x86_64-linux-gnu'
mkdir -p "$work_dir/payload/$archive_root"
archive="$work_dir/cpkt-0.0.0-x86_64-linux-gnu.tar.gz"
tar --owner=0 --group=0 --numeric-owner \
  -C "$work_dir/payload" \
  -czf "$archive" \
  "$archive_root"
: > "$work_dir/cpkt-0.0.0-CHECKSUMS"

if output=$(bash "$source_dir/scripts/run-package-assertions.sh" \
    -DCPKT_ARCHIVE="$archive" \
    -DCPKT_TARGET_ID=x86_64-linux-gnu \
    -DCPKT_BUNDLE_VERSION=0.0.0 2>&1); then
  printf 'package assertions accepted an archive without required metadata\n%s\n' "$output" >&2
  exit 1
fi

case "$output" in
  *'missing regular manifest for core'*) ;;
  *)
    printf 'package assertions cleanup regression did not reach extraction failure\n%s\n' "$output" >&2
    exit 1
    ;;
esac

if [[ "$parent_existed" -eq 1 ]]; then
  after=$(find "$assertion_parent" -mindepth 1 -printf '%P\n' | LC_ALL=C sort)
  if [[ "$before" != "$after" ]]; then
    printf 'package assertion workspace contents changed after a failed assertion\n' >&2
    exit 1
  fi
elif [[ -e "$assertion_parent" ]]; then
  printf 'failed package assertions left workspace parent: %s\n' "$assertion_parent" >&2
  exit 1
fi

legacy_after=$(find "$source_dir" -mindepth 1 -maxdepth 1 -type d -name 'package-assertions-*' -printf '%f\n' | LC_ALL=C sort)
if [[ "$legacy_before" != "$legacy_after" ]]; then
  printf 'failed package assertions left a repository-root extraction directory\n' >&2
  exit 1
fi

cat > "$work_dir/interrupted.py" <<'PYDRIVER'
import os, pathlib, signal, sys, time
sys.path.insert(0,sys.argv[1]+'/scripts')
import cpkt_archive_assert as archive
from cpkt_operation import operation_fds, run
import subprocess
if 'CPKT_OPERATION_FD' not in os.environ:
    sys.exit(run(sys.argv[1],'core',[sys.executable,__file__,*sys.argv[1:]]))
if '--child' not in sys.argv:
    process=subprocess.Popen([sys.executable,__file__,*sys.argv[1:],'--child'],pass_fds=operation_fds())
    output=pathlib.Path(sys.argv[3]);deadline=time.monotonic()+10
    while not output.exists():
        if process.poll() is not None or time.monotonic()>deadline:raise RuntimeError('signal fixture failed before extraction')
        time.sleep(.01)
    process.send_signal(signal.SIGTERM)
    status=process.wait(timeout=10)
    if status!=143:raise RuntimeError('incorrect signal status '+str(status))
    sys.exit(status)
workspace_record=pathlib.Path(sys.argv[3])
def extract(path,workspace,expected):
    workspace_record.write_text(str(workspace))
    time.sleep(60)
archive.safe_extract=extract
sys.argv=['archive','--archive',sys.argv[2],'--target','x86_64-linux-gnu','--version','0.0.0','--group','core']
archive.main()
PYDRIVER
if output=$(python3 "$work_dir/interrupted.py" "$source_dir" "$archive" "$work_dir/signalled-workspace" 2>&1); then
  printf 'package assertion wrapper reported success after TERM\n%s\n' "$output" >&2
  exit 1
fi

if [[ ! -f "$work_dir/signalled-workspace" ]]; then
  printf 'signal regression did not capture the package assertion workspace\n' >&2
  exit 1
fi
signalled_workspace=$(<"$work_dir/signalled-workspace")
case "$signalled_workspace" in
  "$assertion_parent"/assertion.*) ;;
  *) printf 'package assertions used scratch outside build/: %s\n' "$signalled_workspace" >&2; exit 1 ;;
esac
if [[ -e "$signalled_workspace" ]]; then
  printf 'terminated package assertions left workspace: %s\n' "$signalled_workspace" >&2
  exit 1
fi

clean_fixture="$work_dir/clean-fixture"
mkdir -p \
  "$clean_fixture/scripts" \
  "$clean_fixture/build" \
  "$clean_fixture/.cache" \
  "$clean_fixture/dist" \
  "$clean_fixture/package-assertions-stale"
cp -a "$source_dir/scripts" "$clean_fixture/"
cp -a "$source_dir/cmake" "$clean_fixture/"
cp "$source_dir/CMakePresets.json" "$clean_fixture/"
for fd_name in CPKT_OPERATION_FD CPKT_OPERATION_CAP_FD; do
  if [[ -n ${!fd_name:-} ]]; then eval "exec ${!fd_name}>&-"; fi
done
for key in ${!CPKT_OPERATION_@}; do unset "$key"; done
unset GROUP
bash "$clean_fixture/scripts/clean.sh" all
for removed_path in .cache dist package-assertions-stale; do
  if [[ -e "$clean_fixture/$removed_path" ]]; then
    printf 'clean left generated package assertion state: %s\n' "$removed_path" >&2
    exit 1
  fi
done

printf '[test] package assertion workspace cleanup passed\n'
