#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
[ "$cpkt_owner" = misc ] || cpkt_fail 'this operation belongs to cpktmisc'
original_arguments=("$@")
action=${1:?misc action required}
shift
preset=${PRESET:-debug}
explicit=no
while [ "$#" -gt 0 ]; do
  [ "$#" -ge 2 ] || cpkt_fail "missing value for $1"
  case "$1" in --preset) preset=$2 ;; --preset-explicit) explicit=$2 ;; *) cpkt_fail "unknown misc option: $1" ;; esac
  shift 2
done
export GROUP=misc
cpkt_locked "${original_arguments[@]}"
case "$action" in
  prerelease-live)
    [ "${CPKT_LIVE_CHECKS:-0}" = 1 ] || cpkt_fail 'prerelease-live requires CPKT_LIVE_CHECKS=1'
    bash "$0" e2e-sus --preset debug
    bash "$0" e2e-cpktxscribe --preset debug
    exit 0 ;;
  cpktxscribe) target=cpktxscribe ;;
  e2e-sus) target=cpkt_sus_audio_integration_test ;;
  e2e-cpktxscribe) target=cpktxscribe ;;
  example-*) target="cpkt_${action#example-}_c89_example"; target=${target//-/_} ;;
  *) cpkt_fail "unsupported lifecycle action: $action" ;;
esac
if [ "$explicit" = no ]; then
  case "$action" in
    example-*-static) preset=${STATIC_LIVE_PRESET:-x86_64-linux-musl-release} ;;
    e2e-sus|e2e-cpktxscribe) preset=${E2E_SUS_PRESET:-debug} ;;
  esac
fi
cpkt_preset "$preset"
bash "$cpkt_scripts/build.sh" build --group misc --preset "$preset" --target "$target"
case "$action" in
  cpktxscribe) exit 0 ;;
  e2e-sus) bash "$cpkt_scripts/e2e-sus.sh" "$cpkt_binary/cpkt_sus_audio_integration_test" "$cpkt_binary" ;;
  e2e-cpktxscribe) bash "$cpkt_scripts/e2e-cpktxscribe.sh" "$cpkt_binary/tools/cpktxscribe" "$cpkt_binary" ;;
  example-*-static) printf '%s\n' "$cpkt_binary/$target" ;;
  example-*-intro) bash "$cpkt_scripts/run-${action#example-}.sh" "$cpkt_binary/$target" "$cpkt_binary" ;;
  example-*)
    # Arguments are deliberately an argv list; do not evaluate a shell program.
    if [ "$#" -ne 0 ]; then cpkt_fail 'unexpected example arguments'; fi
    "$cpkt_binary/$target" ;;
esac
