#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
# Shared provisioning supervision and local-byte lookup for toolchain archives.

cpkt_provision_with_signals() {
  # Nested ensure calls stay in the existing owned group. This is internal
  # operation state, not a public environment override.
  [[ ${CPKT_PROVISIONING_WORKER:-} != 1 ]] || return 0
  local worker='' cancelled=0 status attempt
  trap 'cancelled=129; [[ -z $worker ]] || kill -TERM -- "-$worker" 2>/dev/null || :' HUP
  trap 'cancelled=130; [[ -z $worker ]] || kill -TERM -- "-$worker" 2>/dev/null || :' INT
  trap 'cancelled=143; [[ -z $worker ]] || kill -TERM -- "-$worker" 2>/dev/null || :' TERM
  set -m
  CPKT_PROVISIONING_WORKER=1 "$BASH" +m "$0" "$@" &
  worker=$!
  set +m
  (( cancelled == 0 )) || kill -TERM -- "-$worker" 2>/dev/null || :
  if wait "$worker"; then status=0; else status=$?; fi
  if (( cancelled != 0 )); then
    for attempt in {1..20}; do
      kill -0 -- "-$worker" 2>/dev/null || break
      sleep 0.1 || :
    done
    kill -KILL -- "-$worker" 2>/dev/null || :
    wait "$worker" 2>/dev/null || :
    status=$cancelled
  fi
  trap - HUP INT TERM
  exit "$status"
}

# Call archive lookup under the resolver's cache lock; the caller owns extraction.

cpkt_cached_archive_sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  else
    printf 'cpkt-archive-cache: sha256sum or shasum is required\n' >&2
    return 1
  fi
}

# Leave a verified destination, or no destination on a genuine miss. Any I/O
# error is fatal: never hide a failed cache reuse behind a network download.
cpkt_restore_cached_archive() (
  local destination=$1 expected=$2 candidate actual temporary
  if [[ -f "$destination" ]]; then
    actual=$(cpkt_cached_archive_sha256 "$destination") || exit 1
    [[ "$actual" != "$expected" ]] || exit 0
    printf 'cpkt-archive-cache: discarding corrupt cached archive: %s\n' "$destination" >&2
    rm -f -- "$destination" || exit 1
  fi
  for candidate in "$(dirname -- "$destination")"/*; do
    [[ -f "$candidate" ]] || continue
    case "${candidate##*/}" in .*|*.tmp.*|*.part-*) continue ;; esac
    actual=$(cpkt_cached_archive_sha256 "$candidate") || exit 1
    [[ "$actual" == "$expected" ]] || continue
    temporary=$(mktemp "${destination}.tmp.XXXXXXXXXX") || exit 1
    trap 'rm -f -- "$temporary"' EXIT
    trap 'exit 1' HUP INT TERM
    cp -- "$candidate" "$temporary" || exit 1
    actual=$(cpkt_cached_archive_sha256 "$temporary") || exit 1
    if [[ "$actual" != "$expected" ]]; then
      printf 'cpkt-archive-cache: cached archive changed during reuse: %s\n' "$candidate" >&2
      exit 1
    fi
    mv -- "$temporary" "$destination" || exit 1
    exit 0
  done
  exit 0
)
