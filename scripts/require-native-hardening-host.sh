#!/usr/bin/env bash
set -euo pipefail

surface=${1:?usage: require-native-hardening-host.sh <surface>}
case "$(uname -s)" in
  Linux) ;;
  *)
    printf '%s is native x86_64 Linux-only; it does not run through cross-compilation, emulation, or QEMU\n' "$surface" >&2
    exit 1
    ;;
esac
case "$(uname -m)" in
  x86_64|amd64) ;;
  *)
    printf '%s is native x86_64 Linux-only; it does not run through cross-compilation, emulation, or QEMU\n' "$surface" >&2
    exit 1
    ;;
esac
