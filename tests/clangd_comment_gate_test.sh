#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'usage: clangd_comment_gate_test.sh <verify-clangd-surface.sh>\n' >&2
  exit 2
fi

checker=$1
owner=core
repo_dir=$(CDPATH= cd -- "$(dirname -- "$checker")/.." && pwd)
mkdir -p "$repo_dir/build"
work_dir=$(mktemp -d "$repo_dir/build/clangd-comment-fixture.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

source_dir="$work_dir/source"
build_dir="$work_dir/build"
mkdir -p "$source_dir/include/cpkt" "$source_dir/src" "$source_dir/examples" "$source_dir/tests" \
  "$build_dir/generated/opcua/cpkt" "$build_dir/generated/lua/include/cpkt" "$work_dir/bin"
mkdir -p "$source_dir/scripts" "$source_dir/cmake"
cp -a "$repo_dir/scripts" "$source_dir/"
cp -a "$repo_dir/cmake" "$source_dir/"
python3 - "$source_dir" "$owner" <<'PY'
import json,sys
from pathlib import Path
root=Path(sys.argv[1])
inputs=['include/cpkt/facade.h','src/facade.c','src/private.c']
hover=['examples/abi_smoke.c']
(root/'cmake/components.json').write_text(json.dumps({'repository_group':sys.argv[2],'groups':{
    group:{'verification_inputs':inputs} for group in (sys.argv[2],)},
    'hover':{group:hover for group in (sys.argv[2],)}}))
PY

printf '#!/usr/bin/env bash\nprintf "%%s\\n" "$*" >> "$CPKT_CLANGD_FIXTURE_RECORD"\nexit 0\n' > "$work_dir/bin/clangd"
chmod +x "$work_dir/bin/clangd"
export CPKT_CLANGD_FIXTURE_RECORD="$work_dir/clangd-calls"
# Simulate the macOS system shell's unavailable Bash-4 builtin. The real gate
# must execute all hover inputs without relying on mapfile or readarray.
mapfile() { printf 'mapfile is unavailable in system Bash 3.2\n' >&2; return 127; }
readarray() { printf 'readarray is unavailable in system Bash 3.2\n' >&2; return 127; }
export -f mapfile readarray

write_header() {
  local comment=$1
  printf '#ifdef __cplusplus\nextern "C" {\n#endif\n%s\nvoid cpkt_documented(void);\n#ifdef __cplusplus\n}\n#endif\n' "$comment" > "$source_dir/include/cpkt/facade.h"
}

write_source() {
  local comment=$1
  printf '%s\nvoid cpkt_documented(void) {}\n' "$comment" > "$source_dir/src/facade.c"
}

write_lua_header() {
  local comment=$1
  {
    local index
    for ((index = 0; index < 156; index++)); do
      printf '%s\nCPKT_LUA_API void cpkt_lua_fixture_%d(void);\n' "$comment" "$index"
    done
  } > "$build_dir/generated/lua/include/cpkt/lua.h"
}

for source_file in \
  examples/abi_smoke.c \
  examples/audio-sus-c89/main.c \
  examples/audio-vox-intro-c89/main.c \
  examples/sus-vox-intro-c89/main.c \
  examples/lua-runtime-c89/main.c \
  examples/lua-runtime-c89/host_module.c \
  examples/opcua-c89/main.c \
  tests/opcua_logging_test.c \
  tests/opcua_types_test.c \
  tests/pdf_facade_test.c; do
  mkdir -p "$(dirname "$source_dir/$source_file")"
  : > "$source_dir/$source_file"
done
python3 - "$source_dir" "$build_dir" <<'PY'
import json,shutil,sys
from pathlib import Path
source,build=map(Path,sys.argv[1:])
entries=[{'directory':str(build),'file':str(path),'arguments':[shutil.which('cc'),'-c',str(path)]}
         for path in source.rglob('*.c') if path.name not in ('facade.c','private.c')]
(build/'compile_commands.json').write_text(json.dumps(entries))
PY

run_gate() {
  local selected key owned_build selector=${1:-$owner}
  (
    for key in ${!CPKT_OPERATION_@}; do unset "$key"; done
    for selected in "$selector"; do
      owned_build="$source_dir/build/x86_64-linux-gnu/$owner/Debug"
      mkdir -p "$owned_build"
      cp -R "$build_dir/." "$owned_build/"
      printf 'CPKT_TARGET_ID:INTERNAL=x86_64-linux-gnu\nCMAKE_BUILD_TYPE:STRING=Debug\nCPKT_GROUP:STRING=%s\nCMAKE_C_COMPILER:FILEPATH=%s\n' "$owner" "$(command -v cc)" > "$owned_build/CMakeCache.txt"
      GROUP="$selected" PATH="$work_dir/bin:$PATH" bash "$checker" "$source_dir" "$owned_build" || exit "$?"
    done
  )
}

printf '/** Generated schema declaration. */\nvoid cpkt_opcua_fixture(void);\n' > "$build_dir/generated/opcua/cpkt/opcua_types.h"
printf '/** Generated constants. */\n' > "$build_dir/generated/opcua/cpkt/opcua_constants.h"
printf '/** Generated plugin declarations. */\n' > "$build_dir/generated/opcua/cpkt/opcua_plugins.h"

write_header '/** Public facade declaration. */'
write_source '/** Public facade definition. */'
: > "$source_dir/src/private.c"
write_lua_header '/** Generated Lua facade declaration. */'
# Sibling generated headers must not be needed by either owning selector.
if [[ $owner != misc ]]; then rm -rf "$build_dir/generated/opcua"; fi
if [[ $owner != core ]]; then rm -rf "$build_dir/generated/lua"; fi
run_gate
run_gate all
test "$(wc -l < "$CPKT_CLANGD_FIXTURE_RECORD")" -eq 2
grep -F -- "$source_dir/examples/abi_smoke.c" "$CPKT_CLANGD_FIXTURE_RECORD" >/dev/null

# Cross-file helpers have private visibility, so they are not public API.
printf 'CPKT_OPCUA_PRIVATE void cpkt_private_helper(void);\n' > "$source_dir/src/opcua_facade_internal.h"
printf 'void cpkt_private_helper(void) {}\n' > "$source_dir/src/private.c"
run_gate

# A helper named in a public header still requires a documented definition.
printf '/** Public helper. */\nvoid cpkt_private_helper(void);\n' >> "$source_dir/include/cpkt/facade.h"
if run_gate >"$work_dir/private.out" 2>"$work_dir/private.err"; then
  printf 'clangd comment gate exempted a public function as a private helper\n' >&2
  exit 1
fi
grep -F 'public facade symbol is missing an adjacent Doxygen comment' "$work_dir/private.err" >/dev/null
write_header '/** Public facade declaration. */'

write_header '/* Ordinary block comment is not Doxygen. */'
if run_gate >"$work_dir/header.out" 2>"$work_dir/header.err"; then
  printf 'clangd comment gate accepted an ordinary header block comment\n' >&2
  exit 1
fi
grep -F 'public facade symbol is missing an adjacent Doxygen comment' "$work_dir/header.err" >/dev/null

write_header '/** Public facade declaration. */'
write_source '/* Ordinary block comment is not Doxygen. */'
if run_gate all >"$work_dir/source.out" 2>"$work_dir/source.err"; then
  printf 'clangd comment gate accepted an ordinary source block comment\n' >&2
  exit 1
fi
grep -F 'public facade symbol is missing an adjacent Doxygen comment' "$work_dir/source.err" >/dev/null

write_source '/** Public facade definition. */'
if [[ $owner == core ]]; then
write_lua_header '/* Ordinary block comment is not Doxygen. */'
if run_gate >"$work_dir/lua.out" 2>"$work_dir/lua.err"; then
  printf 'clangd comment gate accepted undocumented generated Lua declarations\n' >&2
  exit 1
fi
grep -F 'public facade symbol is missing an adjacent Doxygen comment' "$work_dir/lua.err" >/dev/null

write_header '/** Public facade declaration. */'
write_source '/** Public facade definition. */'
write_lua_header '/** Generated Lua facade declaration. */'
fi

if [[ $owner == misc ]]; then
printf 'void cpkt_opcua_fixture(void);\n' > "$build_dir/generated/opcua/cpkt/opcua_types.h"
if run_gate >"$work_dir/opcua.out" 2>"$work_dir/opcua.err"; then
  printf 'clangd comment gate accepted undocumented generated OPC UA declaration\n' >&2
  exit 1
fi
grep -F 'public facade symbol is missing an adjacent Doxygen comment' "$work_dir/opcua.err" >/dev/null

printf '/** Generated schema declaration. */\nvoid cpkt_opcua_fixture(void);\n' > "$build_dir/generated/opcua/cpkt/opcua_types.h"
printf 'void cpkt_opcua_plugin_fixture(void);\n' > "$build_dir/generated/opcua/cpkt/opcua_plugins.h"
if run_gate >"$work_dir/plugins.out" 2>"$work_dir/plugins.err"; then
  printf 'clangd comment gate accepted undocumented generated OPC UA plugin declaration\n' >&2
  exit 1
fi
grep -F 'public facade symbol is missing an adjacent Doxygen comment' "$work_dir/plugins.err" >/dev/null

fi
