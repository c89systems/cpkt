#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
root=${1:?repository root required}
phase=${2:?phase required}
shift 2
[ "${1:-}" = -- ] || { printf 'package command delimiter required\n' >&2; exit 2; }
shift
[ "$#" -gt 0 ] || exit 2
command=("$@")
child=
received=
started=$SECONDS
grace=${_CPKT_PACKAGE_TERMINATION_GRACE_SECONDS:-2}
limit=$(awk -v seconds="$grace" 'BEGIN { if (seconds <= 0 || seconds > 10) exit 2; printf "%d", seconds * 100 }')
export _CPKT_PACKAGE_TERMINATION_GRACE_SECONDS=$(awk -v seconds="$grace" 'BEGIN { print seconds / 2 }')
stop_child() {
  local attempt
  [ -n "$child" ] || return 0
  kill -TERM -- "-$child" 2>/dev/null || true
  for ((attempt=0; attempt<limit; attempt++)); do
    kill -0 -- "-$child" 2>/dev/null || break
    sleep 0.01
  done
  kill -KILL -- "-$child" 2>/dev/null || true
  wait "$child" 2>/dev/null || true
  child=
}
finish() {
  status=$?
  trap - EXIT
  trap '' HUP INT TERM
  stop_child
  if [ "$status" -ne 0 ]; then
    control="$root/build/control"
    mkdir -p "$control"
    marker="$control/package-diagnostic-${CPKT_OPERATION_RUN:-$$}"
    if (set -o noclobber; : > "$marker") 2>/dev/null; then
      if [ -n "$received" ]; then kind=INTERRUPTED; else kind=FAILED; fi
      printf '[package] %s target=%s phase=%s status=%s pid=%s elapsed=%ss' \
        "$kind" "${CPKT_PRESET:-${PRESET:-unknown}}" "$phase" "$status" "$$" "$((SECONDS-started))" >&2
      if [ -n "$received" ]; then
        printf ' received %s; sender identity is unavailable\n' "$received" >&2
      else
        printf ' command=' >&2
        printf '%q ' "${command[@]}" >&2
        printf '\n' >&2
        if [ "$status" -gt 128 ]; then
          signal=$(kill -l "$((status-128))" 2>/dev/null || printf unknown)
          printf '[package] Status %s can represent SIG%s or an explicit exit(%s); exit status alone does not identify a signal sender.\n' "$status" "$signal" "$status" >&2
        fi
      fi
    fi
  fi
  exit "$status"
}
trap finish EXIT
trap 'received=SIGHUP; exit 129' HUP
trap 'received=SIGINT; exit 130' INT
trap 'received=SIGTERM; exit 143' TERM
set -m
"$@" &
child=$!
if wait "$child"; then status=0; else status=$?; fi
exit "$status"
