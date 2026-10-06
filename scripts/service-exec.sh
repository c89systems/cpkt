#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
# Persistent services must never keep a repository operation lock alive.
for key in CPKT_OPERATION_FD CPKT_OPERATION_CAP_FD; do
  descriptor=${!key:-}
  if [[ $descriptor =~ ^[0-9]+$ ]]; then
    # Values are authenticated by the caller and restricted to numeric FDs.
    # Use the issued descriptor numbers, including recovered/narrowed handles.
    eval "exec ${descriptor}>&-"
  fi
done
for key in ${!CPKT_OPERATION_@}; do unset "$key"; done
exec "$@"
