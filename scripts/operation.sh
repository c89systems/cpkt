#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"

group=${GROUP:-all}
root=$cpkt_root
check=no
timeout=${CPKT_OPERATION_TIMEOUT:-120}
source_root=
while [ "$#" -gt 0 ]; do
  case "$1" in
    --root|--group|--source-root|--timeout)
      [ "$#" -ge 2 ] || cpkt_fail "missing value for $1"
      case "$1" in --root) root=$2 ;; --group) group=$2 ;; --source-root) source_root=$2 ;; --timeout) timeout=$2 ;; esac
      shift 2 ;;
    --check) check=yes; shift ;;
    --) shift; break ;;
    *) cpkt_fail "unknown operation option: $1" ;;
  esac
done
root=$(CDPATH= cd -- "$root" && pwd -P)
owner=$("$cpkt_cmake" "-DCPKT_REPO_ROOT=$root" -DCPKT_INFO=owner -P "$cpkt_root/cmake/lifecycle-info.cmake")
case "$group" in all|"$owner") ;; *) cpkt_fail "GROUP=$group is not owned by $root" ;; esac
if [ -n "${CPKT_OPERATION_FD:-}" ]; then
  python3 "$cpkt_scripts/cpkt_lock.py" --root "$root" --group "$group"
  if [ "$check" = yes ]; then [ "$#" -eq 0 ] || cpkt_fail '--check accepts no command'; exit 0; fi
  [ "$#" -gt 0 ] || cpkt_fail 'operation requires a command'
  if [ -n "$source_root" ]; then
    [ "$group" = all ] || cpkt_fail 'source reconstruction requires GROUP=all'
    source_root=$(CDPATH= cd -- "$source_root" && pwd -P)
    [ "$source_root" != "$root" ] && [ -f "$source_root/CMakeLists.txt" ] || cpkt_fail 'invalid reconstruction root'
    unset CPKT_OPERATION_FD CPKT_OPERATION_CAP_FD CPKT_OPERATION_ROOT CPKT_OPERATION_SCOPE CPKT_OPERATION_RUN
    export CPKT_OPERATION_DEPTH=1
    exec bash "$source_root/scripts/operation.sh" --root "$source_root" --group all -- "$@"
  fi
  if [ "$group" != "$CPKT_OPERATION_SCOPE" ]; then
    exec python3 "$cpkt_scripts/cpkt_lock.py" --root "$root" --group "$group" --narrow --command "$@"
  fi
  exec "$@"
fi
[ "$check" = no ] || cpkt_fail 'no inherited repository operation lock'
[ -z "$source_root" ] || cpkt_fail 'source reconstruction requires an outer repository operation'
[ "$#" -gt 0 ] || cpkt_fail 'operation requires a command'
control="$root/build/control"
for path in "$root/build" "$control" "$control/tmp" "$control/operation.lock"; do
  [ ! -L "$path" ] || cpkt_fail "operation path must not be a symlink: $path"
done
mkdir -p "$control"
flock_tool=$(command -v flock || true)
if [ -z "$flock_tool" ] && [ "$(uname -s)" = Darwin ]; then
  flock_tool="$(brew --prefix util-linux)/bin/flock"
fi
[ -x "$flock_tool" ] || cpkt_fail 'flock is required (util-linux; brew install util-linux on macOS)'
[[ "$timeout" =~ ^[0-9]+([.][0-9]+)?$ ]] || cpkt_fail 'invalid operation lock timeout'
exec 9<> "$control/operation.lock"
if ! "$flock_tool" -x -w "$timeout" 9; then
  printf 'Repository operation lock wait expired; owner: ' >&2
  cat "$control/operation.lock" >&2
  exit 2
fi
temporary=
scope_file=
initialized=no
child=
broker=
received_signal=
terminate_child() {
  local attempt limit=40
  [ -n "$child" ] || return 0
  if [ "${CPKT_OPERATION_DEPTH:-0}" != 0 ]; then limit=20; fi
  kill -"${received_signal:-TERM}" -- "-$child" 2>/dev/null || true
  for ((attempt=0; attempt<limit; attempt++)); do
    kill -0 -- "-$child" 2>/dev/null || break
    sleep 0.1
  done
  kill -KILL -- "-$child" 2>/dev/null || true
  wait "$child" 2>/dev/null || true
  child=
}
cleanup() {
  status=$?
  trap - EXIT HUP INT TERM
  terminate_child
  if [ "$initialized" = yes ]; then
    python3 "$cpkt_scripts/cpkt_lock.py" --root "$root" --group "$group" --exit-status "$status" || true
  fi
  if [ -n "$broker" ]; then
    kill -TERM "$broker" 2>/dev/null || true
    wait "$broker" 2>/dev/null || true
    rm -f -- "$control/.operation.sock"
  fi
  if [ -n "$scope_file" ]; then rm -f -- "$scope_file"; fi
  if [ -n "$temporary" ]; then rm -rf -- "$temporary"; fi
  exit "$status"
}
trap cleanup EXIT
trap 'received_signal=HUP; exit 129' HUP
trap 'received_signal=INT; exit 130' INT
trap 'received_signal=TERM; exit 143' TERM
run=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')
temporary="$control/tmp/$run"
mkdir -p "$temporary"
scope_file=$(mktemp "$control/.scope.XXXXXXXX")
printf '%s\0%s\0%s\0' "$root" "$group" "$run" > "$scope_file"
exec 10< "$scope_file"
rm -- "$scope_file"
export CPKT_OPERATION_FD=9 CPKT_OPERATION_CAP_FD=10 CPKT_OPERATION_ROOT="$root"
export CPKT_OPERATION_SCOPE="$group" CPKT_OPERATION_RUN="$run" TMPDIR="$temporary"
export CPKT_OPERATION_BROKER_TOKEN=$(od -An -N32 -tx1 /dev/urandom | tr -d ' \n')

python3 "$cpkt_scripts/cpkt_lock.py" --root "$root" --group "$group" --init --command "$@"
initialized=yes
python3 "$cpkt_scripts/cpkt_lock.py" --root "$root" --group "$group" --broker &
broker=$!
for ((attempt=0; attempt<30; attempt++)); do
  [ ! -S "$control/.operation.sock" ] || break
  kill -0 "$broker" 2>/dev/null || cpkt_fail 'operation descriptor service failed'
  sleep 0.1
done
[ -S "$control/.operation.sock" ] || cpkt_fail 'operation descriptor service did not start'
# Bash job control gives the command a process group on both supported hosts.
set -m
"$@" &
child=$!
if wait "$child"; then status=0; else status=$?; fi
terminate_child
exit "$status"
