#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"

action=${1:-build}
if [ "$#" -gt 0 ]; then shift; fi
group=${GROUP:-all}
preset=${PRESET:-$(cpkt_default_preset)}
fresh=no
dependency=
regex=
label=
targets=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --fresh) fresh=yes; shift ;;
    --group|--preset|--dependency|--regex|--label|--target)
      [ "$#" -ge 2 ] || cpkt_fail "missing value for $1"
      case "$1" in
        --group) group=$2 ;;
        --preset) preset=$2 ;;
        --dependency) dependency=$2 ;;
        --regex) regex=$2 ;;
        --label) label=$2 ;;
        --target) targets+=("$2") ;;
      esac
      shift 2 ;;
    *) cpkt_fail "unknown build argument: $1" ;;
  esac
done
case "$action" in configure|build|test|memcheck|deps|path|preflight) ;; *) cpkt_fail "unknown build action: $action" ;; esac
cpkt_check_group "$group"
cpkt_preset "$preset"
if [ "$action" = memcheck ]; then
  [ "$preset" = valgrind ] || cpkt_fail 'Memcheck requires PRESET=valgrind'
  exec bash "$cpkt_scripts/memcheck.sh" --group "$group" --preset valgrind --regex "$regex"
fi
[ "$group" = all ] || [ "${GROUP:-all}" = all ] || [ "${GROUP:-all}" = "$group" ] || cpkt_fail 'conflicting GROUP selectors'
if [ -n "$dependency" ]; then
  [ "$action" = deps ] || cpkt_fail '--dependency requires deps'
  "$cpkt_cmake" "-DCPKT_REPO_ROOT=$cpkt_root" -DCPKT_INFO=dependency \
    "-DCPKT_DEPENDENCY=$dependency" -P "$cpkt_root/cmake/lifecycle-info.cmake" >/dev/null
fi
if [ "${#targets[@]}" -gt 0 ]; then
  [ "$action" = build ] || cpkt_fail '--target requires build'
  for target in "${targets[@]}"; do
    (cd "$cpkt_root" && "$cpkt_cmake" -DCPKT_INFO=target "-DCPKT_TARGET=$target" \
      -P "$cpkt_root/cmake/lifecycle-info.cmake") >/dev/null
  done
fi
if [ -n "$regex$label" ]; then
  case "$action" in test|memcheck) ;; *) cpkt_fail 'test selectors require a test action' ;; esac
fi
if [ -n "$label" ] && [ "$action" != test ]; then
  cpkt_fail '--label requires the test action'
fi
if [ "$fresh" = yes ]; then
  case "$action" in configure|build) ;; *) cpkt_fail '--fresh requires configure or build' ;; esac
fi
if [ "$action" = path ]; then printf '%s\n' "$cpkt_binary"; exit 0; fi
cpkt_owned_path "$cpkt_binary"
cpkt_owned_path "$cpkt_root/build/$cpkt_target/$cpkt_owner/producer/CMakeCache.txt"
export GROUP="$group"
cd "$cpkt_root"
if [ "$action" = preflight ]; then
  exec bash "$cpkt_scripts/preflight.sh" --group "$group" --preset "$preset"
fi

native_engine=ninja
if [ "${CPKT_SOURCE_GENERATOR:-Ninja}" = 'Unix Makefiles' ]; then native_engine=make; fi
native_arguments=("-DCMAKE_MAKE_PROGRAM=$(command -v "$native_engine")")
if [ "$cpkt_target" = arm64-apple-darwin ] && [ "$(uname -s)" = Darwin ]; then
  export SDKROOT=$(xcrun --show-sdk-path)
  native_arguments+=("-DCMAKE_C_COMPILER=$(xcrun --find clang)" "-DCMAKE_CXX_COMPILER=$(xcrun --find clang++)"
    "-DCMAKE_OSX_SYSROOT=$SDKROOT")
  for tool in mig migcom otool nm ar; do
    value=$(xcrun --find "$tool")
    case "$tool" in mig) native_arguments+=("-DCPKT_DARWIN_HOST_MIG=$value") ;;
      migcom) native_arguments+=("-DCPKT_DARWIN_HOST_MIGCOM=$value") ;;
      nm) native_arguments+=("-DCMAKE_NM=$value") ;;
      ar) native_arguments+=("-DCMAKE_AR=$value") ;;
      otool) native_arguments+=("-DCPKT_OTOOL=$value" "-DCMAKE_OTOOL=$value") ;; esac
  done
fi
consumer_job_arguments=()
producer_job_arguments=()
producer="$cpkt_root/build/$cpkt_target/$cpkt_owner/producer"
if [ -n "${CPKT_DEPENDENCY_BUILD_JOBS:-${CMAKE_BUILD_PARALLEL_LEVEL:-}}" ]; then
  jobs=$(cpkt_jobs "$cpkt_binary")
  consumer_job_arguments+=("-DCPKT_DEPENDENCY_BUILD_JOBS=$jobs")
  producer_job_arguments+=("-DCPKT_DEPENDENCY_BUILD_JOBS=$jobs")
else
  if cpkt_cache_value CPKT_DEPENDENCY_BUILD_JOBS "$cpkt_binary/CMakeCache.txt" >/dev/null; then
    consumer_job_arguments+=("-DCPKT_DEPENDENCY_BUILD_JOBS=$(cpkt_jobs "$cpkt_binary")")
  fi
  if cpkt_cache_value CPKT_DEPENDENCY_BUILD_JOBS "$producer/CMakeCache.txt" >/dev/null; then
    producer_job_arguments+=("-DCPKT_DEPENDENCY_BUILD_JOBS=$(cpkt_jobs "$producer")")
  fi
fi
if [ "${CPKT_SOURCE_RECONSTRUCTION:-0}" = 1 ]; then
  case "${CPKT_SOURCE_GENERATOR:-Ninja}" in Ninja|'Unix Makefiles') ;; *) cpkt_fail 'unsupported reconstruction generator' ;; esac
  native_arguments+=(-G "${CPKT_SOURCE_GENERATOR:-Ninja}" "-DCPKT_DEPENDENCY_CACHE=${CPKT_DEPENDENCY_CACHE:?}")
fi

prepare_dependencies() {
  local producer="$cpkt_root/build/$cpkt_target/$cpkt_owner/producer" requested=cpkt_deps_all
  if [ "$cpkt_owner" = core ] && [[ "$cpkt_target" == *-linux-* ]]; then
    report=$(bash "$cpkt_scripts/cpkt-toolchains.sh" discover "$cpkt_target")
    if [[ $'\n'"$report"$'\n' != *$'\nstatus=ready\n'* ]]; then
      bash "$cpkt_scripts/cpkt-toolchains.sh" ensure "$cpkt_target"
    fi
  fi
  for key in CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS CMAKE_STATIC_LINKER_FLAGS; do
    if value=$(cpkt_cache_value "$key" "$cpkt_binary/CMakeCache.txt"); then
      producer_job_arguments+=("-D$key=$value")
    fi
  done
  "$cpkt_cmake" --preset "$preset" -B "$producer" -DCPKT_DEPENDENCY_PRODUCER=ON \
    -DCPKT_BUILD_DEPENDENCIES=ON -DCMAKE_BUILD_TYPE=Release -DCPKT_BUILD_TESTS=OFF \
    -DCPKT_FACADE_ONLY=OFF -DCPKT_ENABLE_FUZZING=OFF -DCPKT_GROUP="$cpkt_owner" \
    "${native_arguments[@]}" "${producer_job_arguments[@]}"
  if [ -n "$dependency" ]; then requested="cpkt_deps_$dependency"; fi
  "$cpkt_cmake" --build "$producer" --parallel "$(cpkt_jobs "$producer")" --target "$requested"
}

if [ "$fresh" = yes ]; then
  bash "$cpkt_scripts/clean.sh" graph --path "$cpkt_binary"
fi
case "$preset" in
  valgrind|fuzz)
    [ -f "$cpkt_root/build/$cpkt_target/$cpkt_owner/Debug/CMakeCache.txt" ] || cpkt_fail "prepare native Debug before hardening" ;;
  *) prepare_dependencies ;;
esac
if [ "$action" = deps ]; then exit 0; fi
"$cpkt_cmake" --preset "$preset" -B "$cpkt_binary" -DCPKT_DEPENDENCY_PRODUCER=OFF \
  -DCPKT_BUILD_DEPENDENCIES=OFF -DCPKT_GROUP="$cpkt_owner" \
  "${native_arguments[@]}" "${consumer_job_arguments[@]}"
[ "$action" != configure ] || exit 0
arguments=(--build "$cpkt_binary" --parallel "$(cpkt_jobs "$cpkt_binary")")
if [ "${#targets[@]}" -gt 0 ]; then arguments+=(--target "${targets[@]}"); fi
"$cpkt_cmake" "${arguments[@]}"
[ "$action" != build ] || exit 0
arguments=(--test-dir "$cpkt_binary" --no-tests=error --stop-on-failure --output-on-failure)
if [ -n "$regex$label" ]; then
  if [ -n "$regex" ]; then arguments+=(-R "$regex"); fi
  if [ -n "$label" ]; then arguments+=(-L "$label"); fi
  "$cpkt_ctest" "${arguments[@]}"
else
  if [ "${CPKT_TEST_RERUN:-0}" = 1 ]; then
    cpkt_owned_path "$cpkt_binary/verification"
    rm -f -- "$cpkt_binary/verification/host.passed" "$cpkt_binary/verification/runtime.passed"
  fi
  if [ "$cpkt_configuration" = Debug ] && [ "$cpkt_target" = x86_64-linux-gnu ]; then
    "$cpkt_cmake" --build "$cpkt_binary" --target cpkt_verify_host
  elif [ "$preset" = arm64-apple-darwin-debug ]; then
    "$cpkt_cmake" --build "$cpkt_binary" --target cpkt_verify_host
  fi
  "$cpkt_cmake" --build "$cpkt_binary" --target cpkt_verify_runtime
fi
