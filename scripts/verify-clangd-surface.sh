#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="${1:?usage: verify-clangd-surface.sh <source-dir> <build-dir>}"
BUILD_DIR="${2:?usage: verify-clangd-surface.sh <source-dir> <build-dir>}"
COMPILE_COMMANDS="${BUILD_DIR}/compile_commands.json"
group=${GROUP:-all}
if [[ $# -gt 2 ]]; then
  [[ $# -eq 4 && $3 == --group ]] || { printf 'usage: verify-clangd-surface.sh <source-dir> <build-dir> [--group core|all]\n' >&2; exit 2; }
  [[ $group == all || $group == "$4" ]] || { printf 'conflicting GROUP selectors\n' >&2; exit 2; }
  group=$4
fi
case "$group" in core|all) ;; *) printf 'unknown GROUP: %s\n' "$group" >&2; exit 2 ;; esac
export GROUP="$group"
if [[ -z ${CPKT_OPERATION_FD:-} ]]; then
  exec python3 "${SOURCE_DIR}/scripts/cpkt_operation.py" --group "$group" -- bash "$0" "$SOURCE_DIR" "$BUILD_DIR"
fi
python3 "${SOURCE_DIR}/scripts/cpkt_operation.py" --group "$group" --check
host_os=$(uname -s)
expected_target=x86_64-linux-gnu
if [[ $host_os == Darwin ]]; then expected_target=arm64-apple-darwin; fi
if ! grep -q "^CPKT_TARGET_ID:.*=$expected_target$" "$BUILD_DIR/CMakeCache.txt" ||
    ! grep -Eq '^CMAKE_BUILD_TYPE:.*=(Debug|Release)$' "$BUILD_DIR/CMakeCache.txt"; then
  printf 'selected clangd surface requires a native Debug or Release configuration\n' >&2
  exit 2
fi

if [[ $group == all ]]; then
  group=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["repository_group"])' "$SOURCE_DIR/cmake/components.json")
fi
export GROUP="$group"

if ! grep -q "^CPKT_GROUP:.*=$group$" "$BUILD_DIR/CMakeCache.txt"; then
  printf 'compile database group does not match GROUP=%s: %s\n' "$group" "$BUILD_DIR" >&2
  exit 2
fi

if [[ ! -f "${COMPILE_COMMANDS}" ]]; then
  printf 'compile database not found: %s\n' "${COMPILE_COMMANDS}" >&2
  exit 1
fi

require_compile_command() {
  local source_file
  source_file="$1"
  if ! grep -F "\"${SOURCE_DIR}/${source_file}\"" "${COMPILE_COMMANDS}" >/dev/null; then
    printf 'compile database does not contain %s\n' "${source_file}" >&2
    exit 1
  fi
}

python3 - "$SOURCE_DIR" "$BUILD_DIR" <<'PY'
import pathlib
import json
import os
import re
import sys

source_dir = pathlib.Path(sys.argv[1])
build_dir = pathlib.Path(sys.argv[2])


def previous_nonblank_is_doxygen_comment(lines, index):
    i = index - 1
    while i >= 0 and not lines[i].strip():
        i -= 1
    if i < 0:
        return False

    stripped = lines[i].lstrip()
    if stripped.startswith(("///", "//!")):
        return True
    if not stripped.endswith("*/"):
        return False

    while i >= 0:
        stripped = lines[i].lstrip()
        if stripped.startswith(("/**", "/*!")):
            return True
        if stripped.startswith("/*"):
            return False
        i -= 1
    return False


def first_declaration_line(lines, start, end):
    i = start
    in_block_comment = False
    while i <= end:
        stripped = lines[i].lstrip()
        if in_block_comment:
            if "*/" in stripped:
                in_block_comment = False
            i += 1
            continue
        if not stripped or stripped.startswith("//"):
            i += 1
            continue
        if stripped.startswith("/*"):
            if "*/" not in stripped:
                in_block_comment = True
            i += 1
            continue
        return i
    return start


def declaration_requires_comment(text):
    stripped = text.lstrip()
    if stripped.startswith(("typedef struct cpkt_", "typedef enum cpkt_")):
        return True
    if stripped.startswith("typedef ") and "cpkt_" in stripped:
        return True
    compact = " ".join(stripped.split())
    return re.match(r"^[A-Za-z_][A-Za-z0-9_\s\*]*\bcpkt_[A-Za-z0-9_]+\s*\(", compact) is not None


def function_name_from_definition(text):
    compact = " ".join(text.split())
    if compact.startswith("static "):
        return None
    match = re.match(r"^[A-Za-z_][A-Za-z0-9_\s\*]*\b(cpkt_[A-Za-z0-9_]+)\s*\(", compact)
    if match:
        return match.group(1)
    return None


def verify_header(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    depth = 0
    start = None
    chunks = []
    for i, line in enumerate(lines):
        # C++ linkage framing is not a type declaration. Count only declaration
        # braces so an extern block cannot hide every public symbol from this gate.
        if depth == 0 and line.strip() in ('extern "C" {', '}'):
            continue
        if depth == 0 and start is None and line.strip() and not line.lstrip().startswith("#"):
            start = i
        depth += line.count("{") - line.count("}")
        if start is not None and depth == 0 and ";" in line:
            declaration_start = first_declaration_line(lines, start, i)
            chunks.append((declaration_start, "\n".join(lines[declaration_start : i + 1])))
            start = None
    # A public header can document a coherent API family with a Doxygen group
    # rather than repeating an empty sentence on every function declaration.
    # SQLite's broad surface uses group contracts for ownership and callback
    # lifetime, with declaration-local comments where a contract differs.
    # Existing smaller facade headers may continue using local documentation.
    if "@defgroup cpkt_sqlite" in "\n".join(lines):
        return []
    failures = []
    for start_index, text in chunks:
        if declaration_requires_comment(text) and not previous_nonblank_is_doxygen_comment(lines, start_index):
            failures.append((start_index + 1, text.splitlines()[0].strip()))
    return failures


def verify_source(path, documented_symbols, private_symbols):
    lines = path.read_text(encoding="utf-8").splitlines()
    failures = []
    depth = 0
    start = None
    for i, line in enumerate(lines):
        stripped = line.strip()
        if depth == 0 and start is None:
            if stripped.startswith("static "):
                start = None
            elif re.match(r"^[A-Za-z_][A-Za-z0-9_\s\*]*\bcpkt_[A-Za-z0-9_]+\s*\(", stripped):
                start = i
            elif re.match(r"^cpkt_[A-Za-z0-9_]+$", stripped):
                start = i
        if start is not None and "{" in line:
            text = "\n".join(lines[start : i + 1])
            name = function_name_from_definition(text)
            # SQLite's broad installed header uses a Doxygen group contract;
            # its implementation intentionally does not duplicate it.
            # Every other facade source keeps a declaration-local Doxygen
            # comment, which also protects this verifier's negative fixture.
            if name and not (name in private_symbols and name not in documented_symbols) and (name not in documented_symbols or
                         path.name != "sqlite.c") and not previous_nonblank_is_doxygen_comment(
                             lines, start):
                failures.append((start + 1, name))
            start = None
        depth += line.count("{") - line.count("}")
        if depth == 0 and start is not None and ";" in line:
            start = None
    return failures


inventory = json.loads((source_dir / "cmake/components.json").read_text())
group = os.environ.get("GROUP", "all")
selected_inputs = set()
for owner, item in inventory["groups"].items():
    if group in (owner, "all"):
        selected_inputs.update(item["verification_inputs"])
public_headers = sorted(source_dir / name for name in selected_inputs if name.startswith("include/") and name.endswith(".h"))
facade_sources = sorted(source_dir / name for name in selected_inputs if name.startswith(("src/", "cmake/")) and name.endswith(".c"))

if not public_headers:
    print("no public facade headers found under include/cpkt", file=sys.stderr)
    sys.exit(1)
if not facade_sources:
    print("no facade source translations found under src", file=sys.stderr)
    sys.exit(1)

all_failures = []
documented_symbols = set()
for header in public_headers:
    for line, symbol in verify_header(header):
        all_failures.append((header, line, symbol))
    documented_symbols.update(
        re.findall(r"\b(cpkt_[A-Za-z0-9_]+)\s*\(",
                   header.read_text(encoding="utf-8"))
    )
if group in ("misc", "all"):
    opcua_types_header = build_dir / "generated/opcua/cpkt/opcua_types.h"
    if not opcua_types_header.is_file():
        sys.exit("generated OPC UA C89 schema header is missing")
    for filename in ("opcua_types.h", "opcua_constants.h", "opcua_plugins.h"):
        generated_header = opcua_types_header.with_name(filename)
        if not generated_header.is_file():
            sys.exit(f"generated OPC UA header is missing: {filename}")
        for line, symbol in verify_header(generated_header):
            all_failures.append((generated_header, line, symbol))
        documented_symbols.update(re.findall(r"\b(cpkt_[A-Za-z0-9_]+)\s*\(", generated_header.read_text()))
# The OPC UA implementation has private linkage across several source files.
# Only explicitly hidden helper declarations can be exempt from public API
# comments. The export gate independently verifies their object visibility.
private_header = source_dir / "src/opcua_facade_internal.h"
private_symbols = set()
if private_header.is_file():
    private_symbols.update(re.findall(
        r"CPKT_OPCUA_PRIVATE\s+[A-Za-z_][A-Za-z0-9_\s*]*?\b(cpkt_[A-Za-z0-9_]+)\s*\(",
        private_header.read_text()))

for source in facade_sources:
    for line, symbol in verify_source(source, documented_symbols, private_symbols):
        all_failures.append((source, line, symbol))

# The complete Lua C89 header is generated in the build tree, so the installed
# header scan above cannot see its function comments.
if group in ("core", "all"):
    if 'cpkt_cmocka_${_variant}' in inventory.get('targets', {}):
        cmocka_header = build_dir/'generated/cmocka/include/cpkt/cmocka.h'
        if not cmocka_header.is_file():
            sys.exit('generated C89 cmocka header is missing')
        for line, symbol in verify_header(cmocka_header):
            all_failures.append((cmocka_header,line,symbol))
    lua_header = build_dir / "generated/lua/include/cpkt/lua.h"
    if not lua_header.is_file():
        print(f"generated Lua facade header not found: {lua_header}", file=sys.stderr)
        sys.exit(1)
    lua_lines = lua_header.read_text(encoding="utf-8").splitlines()
    lua_declarations = 0
    for index, line in enumerate(lua_lines):
        if line.startswith("CPKT_LUA_API "):
            lua_declarations += 1
            if not previous_nonblank_is_doxygen_comment(lua_lines, index):
                all_failures.append((lua_header, index + 1, line.strip()))
    if lua_declarations < 156:
        print(f"generated Lua facade has only {lua_declarations} public declarations", file=sys.stderr)
        sys.exit(1)

if all_failures:
    for path, line, symbol in all_failures:
        print(
            f"{path}:{line}: public facade symbol is missing an adjacent Doxygen comment: {symbol}",
            file=sys.stderr,
        )
    sys.exit(1)
PY

if ! command -v clangd >/dev/null 2>&1; then
  printf 'clangd is required; install it with the host OS package manager\n' >&2
  exit 1
fi
hover_inputs=$(python3 - "$SOURCE_DIR" "$group" <<'PYTHON'
import json,sys
from pathlib import Path
inventory=json.loads((Path(sys.argv[1]) / 'cmake/components.json').read_text())
for owner, paths in inventory['hover'].items():
    if sys.argv[2] in (owner, 'all'):
        print('\n'.join(paths))
PYTHON
)
while IFS= read -r source_file; do
  [[ -n $source_file ]] || continue
  require_compile_command "$source_file"
  python3 "$SOURCE_DIR/scripts/cpkt_clangd_check.py" --root "$SOURCE_DIR" \
    --group "$group" --build "$BUILD_DIR" --source "$SOURCE_DIR/$source_file" \
    --gate "$0" --checker "$(command -v clangd)" >/dev/null
done <<< "$hover_inputs"

if grep -q '^CMAKE_BUILD_TYPE:.*=Debug$' "$BUILD_DIR/CMakeCache.txt"; then
  python3 "$SOURCE_DIR/scripts/cpkt_clangd_check.py" --root "$SOURCE_DIR" --group "$group" --publish-editor --native-target "$expected_target"
fi
