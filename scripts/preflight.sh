#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
group=${GROUP:-all}
preset=${PRESET:-debug}
while [ "$#" -gt 0 ]; do
  [ "$#" -ge 2 ] || cpkt_fail "missing value for $1"
  case "$1" in --group) group=$2 ;; --preset) preset=$2 ;; *) cpkt_fail "unknown preflight option: $1" ;; esac
  shift 2
done
cpkt_check_group "$group"
cpkt_preset "$preset"
binary="$cpkt_root/build/control/preflight/$cpkt_target/native"
cpkt_owned_path "$binary"
export GROUP="$group"
libc=${cpkt_target##*-}
profile=linux-runner
arguments=()
case "$cpkt_target" in
  *-linux-*)
    if [[ "$cpkt_target" == x86_64-* ]]; then profile=native-linux; fi
    arguments+=("-DCMAKE_TOOLCHAIN_FILE=$cpkt_root/cmake/CpktReadOnlyToolchain.cmake") ;;
  arm64-apple-darwin)
    libc=
    if [ "$(uname -s)" = Darwin ]; then
      profile=native-darwin
      export SDKROOT=$(xcrun --show-sdk-path)
      arguments+=("-DCMAKE_C_COMPILER=$(xcrun --find clang)" "-DCMAKE_CXX_COMPILER=$(xcrun --find clang++)"
        "-DCMAKE_OSX_SYSROOT=$SDKROOT" -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0)
    else
      profile=osxcross
      arguments+=("-DCMAKE_TOOLCHAIN_FILE=$cpkt_root/cmake/toolchains/arm64-apple-darwin.cmake")
    fi ;;
esac
"$cpkt_cmake" -S "$cpkt_root/cmake/preflight" -B "$binary" -G Ninja \
  "-DCPKT_REPO_ROOT=$cpkt_root" "-DCPKT_TARGET_ID=$cpkt_target" "-DCPKT_GROUP=$cpkt_owner" \
  "-DCPKT_PREFLIGHT_PROFILE=$profile" "-DCPKT_TARGET_ARCH=${cpkt_target%%-*}" \
  "-DCPKT_TARGET_OS=$([ "$cpkt_target" = arm64-apple-darwin ] && printf darwin || printf linux)" \
  "-DCPKT_TARGET_LIBC=$libc" "${arguments[@]}"
"$cpkt_ctest" --test-dir "$binary" --no-tests=error --stop-on-failure --output-on-failure
