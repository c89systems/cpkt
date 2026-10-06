#!/usr/bin/env bash
set -euo pipefail
root=${1:?repository root required}
group=${2:?repository owner required}
shift 2
python3 "$root/scripts/cpkt_lock.py" --root "$root" --group "$group"
[ "$#" -gt 0 ] || exit 0
checking=no
if [ "${1:-}" = bash ] && [ "${2:-}" = "$root/scripts/operation.sh" ]; then
  for argument in "$@"; do if [ "$argument" = --check ]; then checking=yes; fi; done
fi
if [ "$#" -eq 9 ] && [ "${1:-}" = bash ] && [ "${2:-}" = "$root/scripts/package.sh" ] \
    && [ "${3:-}" = package-stage ] && [ "${4:-}" = --group ] && [ "${5:-}" = "$group" ] \
    && [ "${6:-}" = --preset ] && [ -n "${7:-}" ] && [ "${8:-}" = --scope ] && [ "${9:-}" = selected ]; then
  checking=yes
fi
if [ "$checking" = no ]; then
  case "$PWD" in
    "$root/.cache/deps-build/"*)
      relative=${PWD#"$root/.cache/deps-build/"}
      target=${relative%%/*}
      component=${relative#*/}
      component=${component%%/*}
      if [ "$component" = mqtt-c ]; then component=mqttc; fi
      rm -f -- "$root/build/verification/$target/$group/component-$component.json"
      for file in "$root/build/verification/$target/$group/"*-development.json; do rm -f -- "$file"; done ;;
    "$root/build/"*)
      relative=${PWD#"$root/build/"}
      target=${relative%%/*}
      remainder=${relative#*/}
      configured_group=${remainder%%/*}
      remainder=${remainder#*/}
      configuration=${remainder%%/*}
      if [ "$configured_group" = "$group" ]; then
        case "$configuration" in Debug|Release|Fuzz|Valgrind)
          rm -f -- "$root/build/verification/$target/$group/$configuration-development.json" \
            "$root/build/verification/$target/$group/$configuration-built.json" ;;
        esac
      fi ;;
  esac
fi
exec "$@"
