#!/usr/bin/env bash
set -euo pipefail

repo_root=${1:?repository root is required}
cmake_file="$repo_root/CMakeLists.txt"

require_configuration() {
  local target=$1
  local source=$2
  local configure=$3

  if ! awk -v target="$target" -v source="$source" -v configure="$configure" '
    index($0, "cpkt_group_add_library(" target " ") > 0 {
      if (index($0, source) == 0)
        exit 1
      inside = 1
      next
    }
    inside && index($0, "cpkt_group_add_library(") > 0 { exit 1 }
    inside && index($0, configure) > 0 { found = 1; exit }
    END { exit found ? 0 : 1 }
  ' "$cmake_file"; then
    printf '%s must be compiled through %s\n' "$target" "$configure" >&2
    exit 1
  fi
}

for target in \
  cpkt_audio_static cpkt_audio_shared \
  cpkt_sus_static cpkt_sus_shared \
  cpkt_opcua_static cpkt_opcua_shared \
  cpkt_gssapi_static cpkt_gssapi_shared \
  cpkt_sasl_static cpkt_sasl_shared \
  cpkt_postgres_static cpkt_postgres_shared \
  cpkt_sqlite_static cpkt_sqlite_shared; do
  case "$target" in
    cpkt_audio_*) source=src/audio.c ;;
    cpkt_sus_*) source=src/sus.c ;;
    cpkt_opcua_*) source='${CPKT_OPCUA_SOURCES}' ;;
    cpkt_gssapi_*) source=src/gssapi.c ;;
    cpkt_sasl_*) source=src/sasl.c ;;
    cpkt_postgres_*) source=src/postgres.c ;;
    cpkt_sqlite_*) source=src/sqlite.c ;;
  esac
  if ! grep -q "cpkt_group_add_library($target " "$cmake_file"; then continue; fi
  require_configuration "$target" "$source" "cpkt_configure_c89_target($target)"
done

if grep -q "cpkt_group_add_library(cpkt_lua_runtime_static " "$cmake_file"; then
require_configuration cpkt_lua_runtime_static src/lua_runtime.c \
  'cpkt_configure_c89_lua_native_header_target(cpkt_lua_runtime_static)'
require_configuration cpkt_lua_runtime_shared src/lua_runtime.c \
  'cpkt_configure_c89_lua_native_header_target(cpkt_lua_runtime_shared)'
require_configuration cpkt_lua_static '"${CPKT_LUA_FACADE_SOURCE}"' \
  'cpkt_configure_c89_lua_native_header_target(cpkt_lua_static)'
require_configuration cpkt_lua_shared '"${CPKT_LUA_FACADE_SOURCE}"' \
  'cpkt_configure_c89_lua_native_header_target(cpkt_lua_shared)'

fi

if rg -n '(target_compile_features|cpkt_group_target_command\(target_compile_features)\(cpkt_[A-Za-z0-9_]+[[:space:]]+PRIVATE[[:space:]]+c_std_[0-9]+' "$cmake_file"; then
  printf 'production facade targets must not select a post-C89 language standard\n' >&2
  exit 1
fi
