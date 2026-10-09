#!/usr/bin/env bash
# Source this file to retain the parent's actual cache, job and generator choices.
source "$(dirname -- "${BASH_SOURCE[0]}")/lifecycle-common.sh"
source_parent=${1:-$cpkt_root}
source_selected=${CPKT_PRESET:-${PRESET:-debug}}
source_configuration=Release
case "$source_selected" in debug) source_configuration=Debug ;; valgrind) source_configuration=Valgrind ;; fuzz) source_configuration=Fuzz ;; esac
source_cache="$source_parent/build/x86_64-linux-gnu/$cpkt_owner/$source_configuration/CMakeCache.txt"
source_limit=$(cpkt_cache_value CPKT_DEPENDENCY_BUILD_JOBS "$source_cache") || source_limit=8
[[ "$source_limit" =~ ^[1-9][0-9]*$ ]] && [ "$source_limit" -le 8 ] || cpkt_fail 'invalid configured reconstruction job limit'
source_jobs=${CPKT_DEPENDENCY_BUILD_JOBS:-${CMAKE_BUILD_PARALLEL_LEVEL:-$source_limit}}
[[ "$source_jobs" =~ ^[1-9][0-9]*$ ]] && [ "$source_jobs" -le "$source_limit" ] || cpkt_fail "reconstruction jobs exceed configured limit $source_limit"
source_shared=${CPKT_DEPENDENCY_CACHE:-}
if [ -z "$source_shared" ]; then
  source_shared=$(cpkt_cache_value CPKT_DEPENDENCY_CACHE "$source_cache") || source_shared="${XDG_CACHE_HOME:-$HOME/.cache}/cpkt/deps"
fi
source_generator=${CPKT_SOURCE_GENERATOR:-}
if [ -z "$source_generator" ]; then
  source_generator=$(cpkt_cache_value CMAKE_GENERATOR "$source_cache") || source_generator=Ninja
fi
case "$source_generator" in Ninja|'Unix Makefiles') ;; *) cpkt_fail 'unsupported reconstruction generator' ;; esac
export CPKT_DEPENDENCY_BUILD_JOBS="$source_jobs" CMAKE_BUILD_PARALLEL_LEVEL="$source_jobs"
export CPKT_DEPENDENCY_CACHE="$source_shared" CPKT_SOURCE_GENERATOR="$source_generator"
export CPKT_SOURCE_RECONSTRUCTION=1 CPKT_PRESET=x86_64-linux-gnu-release
