#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
group=${GROUP:-all}
preset=valgrind
regex=
while [ "$#" -gt 0 ]; do
  [ "$#" -ge 2 ] || cpkt_fail "missing value for $1"
  case "$1" in --group) group=$2 ;; --preset) preset=$2 ;; --regex) regex=$2 ;; *) cpkt_fail "unknown Memcheck option $1" ;; esac
  shift 2
done
cpkt_check_group "$group"
[ "$preset" = valgrind ] || cpkt_fail 'Memcheck requires the valgrind profile'
cpkt_preset "$preset"
export GROUP="$group"
cpkt_locked --group "$group" --preset "$preset" --regex "$regex"
bash "$cpkt_scripts/require-native-hardening-host.sh" valgrind
command -v valgrind >/dev/null || cpkt_fail 'host Valgrind is required'
listing="$cpkt_binary/cpkt-memcheck-inventory.json"
arguments=(--test-dir "$cpkt_binary" -L memcheck)
if [ -n "$regex" ]; then arguments+=(-R "$regex"); fi
"$cpkt_ctest" "${arguments[@]}" --show-only=json-v1 > "$listing"
proof=(--root "$cpkt_root" --group "$cpkt_owner" --target "$cpkt_target" \
  --configuration Valgrind --preset valgrind)
python3 "$cpkt_scripts/cpkt_memcheck_evidence.py" inventory "${proof[@]}" --regex "$regex"
rm -f -- "$cpkt_binary/cpkt-memcheck-results.xml"
options='--error-exitcode=1 --leak-check=full --track-origins=yes --show-leak-kinds=definite,indirect'
arguments+=(-T memcheck --no-tests=error --stop-on-failure --output-on-failure \
  --output-junit "$cpkt_binary/cpkt-memcheck-results.xml" --overwrite "MemoryCheckCommandOptions=$options")
suppression=$("$cpkt_cmake" -DCPKT_INFO=memcheck-suppression "-DCPKT_REPO_ROOT=$cpkt_root" \
  -P "$cpkt_root/cmake/lifecycle-info.cmake")
if [ -n "$suppression" ]; then arguments+=(--overwrite "MemoryCheckSuppressionFile=$cpkt_root/$suppression"); fi
"$cpkt_ctest" "${arguments[@]}"
python3 "$cpkt_scripts/cpkt_memcheck_evidence.py" tested "${proof[@]}" --regex "$regex"
if [ "$cpkt_owner" = db ]; then
  CPKT_POSTGRES_E2E_MEMCHECK=1 bash "$cpkt_scripts/test-e2e.sh" "$cpkt_binary/cpkt_postgres_integration_test"
fi
