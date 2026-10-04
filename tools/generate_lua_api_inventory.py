#!/usr/bin/env python3
"""Verify complete Lua public-function facade coverage and native imports."""

import argparse
import json
import pathlib
import re
import subprocess
import sys
from typing import Iterable, List, Set


DECLARATION = re.compile(
    r"\b(?:LUA_API|LUALIB_API|LUAMOD_API)\s+.*?\(\s*"
    r"(?P<name>lua(?:L|open)?_[A-Za-z0-9_]+)\s*\)\s*\(.*?\)\s*;",
    re.DOTALL)
FACADE_DECLARATION = re.compile(
    r"^CPKT_LUA_API[^\n]*\b(cpkt_lua_[A-Za-z0-9_]+)\s*\(", re.MULTILINE)
CONVENIENCE_EQUIVALENTS = {
    "lua_upvalueindex": ("cpkt_lua_upvalueindex",),
    "lua_call": ("cpkt_lua_call",),
    "lua_pcall": ("cpkt_lua_pcall",),
    "lua_yield": ("cpkt_lua_yield",),
    "lua_getextraspace": ("cpkt_lua_getextraspace",),
    "lua_tonumber": ("cpkt_lua_tonumber",),
    "lua_tointeger": ("cpkt_lua_tointeger",),
    "lua_pop": ("cpkt_lua_pop",),
    "lua_newtable": ("cpkt_lua_newtable",),
    "lua_register": ("cpkt_lua_register",),
    "lua_pushcfunction": ("cpkt_lua_pushcfunction",),
    "lua_isfunction": ("cpkt_lua_isfunction",),
    "lua_istable": ("cpkt_lua_istable",),
    "lua_islightuserdata": ("cpkt_lua_islightuserdata",),
    "lua_isnil": ("cpkt_lua_isnil",),
    "lua_isboolean": ("cpkt_lua_isboolean",),
    "lua_isthread": ("cpkt_lua_isthread",),
    "lua_isnone": ("cpkt_lua_isnone",),
    "lua_isnoneornil": ("cpkt_lua_isnoneornil",),
    "lua_pushliteral": ("cpkt_lua_pushliteral",),
    "lua_pushglobaltable": ("cpkt_lua_pushglobaltable",),
    "lua_tostring": ("cpkt_lua_tostring",),
    "lua_insert": ("cpkt_lua_insert",),
    "lua_remove": ("cpkt_lua_remove",),
    "lua_replace": ("cpkt_lua_replace",),
    "lua_newuserdata": ("cpkt_lua_newuserdata",),
    "lua_getuservalue": ("cpkt_lua_getuservalue",),
    "lua_setuservalue": ("cpkt_lua_setuservalue",),
    "lua_resetthread": ("cpkt_lua_resetthread",),
    "luaL_checkversion": ("cpkt_lua_l_checkversion",),
    "luaL_loadfile": ("cpkt_lua_l_loadfile",),
    "luaL_loadbuffer": ("cpkt_lua_l_loadbuffer",),
    "luaL_checkstring": ("cpkt_lua_l_checkstring",),
    "luaL_optstring": ("cpkt_lua_l_optstring",),
    "luaL_typename": ("cpkt_lua_l_typename",),
    "luaL_dofile": ("cpkt_lua_l_dofile",),
    "luaL_dostring": ("cpkt_lua_l_dostring",),
    "luaL_getmetatable": ("cpkt_lua_l_getmetatable",),
    "luaL_opt": ("cpkt_lua_l_opt",),
    "luaL_newlibtable": ("cpkt_lua_l_newlibtable",),
    "luaL_newlib": ("cpkt_lua_l_newlib",),
    "luaL_argcheck": ("cpkt_lua_l_argcheck",),
    "luaL_argexpected": ("cpkt_lua_l_argexpected",),
    "luaL_pushfail": ("cpkt_lua_l_pushfail",),
    "luaL_bufflen": ("cpkt_lua_l_bufflen",),
    "luaL_buffaddr": ("cpkt_lua_l_buffaddr",),
    "luaL_addchar": ("cpkt_lua_l_addchar",),
    "luaL_addsize": ("cpkt_lua_l_addsize",),
    "luaL_buffsub": ("cpkt_lua_l_buffsub",),
    "luaL_prepbuffer": ("cpkt_lua_l_prepbuffer",),
    "luaL_openlibs": ("cpkt_lua_l_openlibs",),
    "luaL_intop": (
        "cpkt_lua_l_integer_add",
        "cpkt_lua_l_integer_subtract",
        "cpkt_lua_l_integer_multiply",
        "cpkt_lua_l_integer_bit_and",
        "cpkt_lua_l_integer_bit_or",
        "cpkt_lua_l_integer_bit_xor",
        "cpkt_lua_l_integer_shift_left",
        "cpkt_lua_l_integer_shift_right",
    ),
}


def read_headers(include_dir: pathlib.Path) -> str:
    names = ("lua.h", "lauxlib.h", "lualib.h")
    contents: List[str] = []
    for name in names:
        path = include_dir / name
        if not path.is_file():
            raise ValueError("required Lua header is missing: " + str(path))
        contents.append(re.sub(r"/\*.*?\*/", "", path.read_text("utf-8"),
                               flags=re.DOTALL))
    return "\n".join(contents)


def public_name(native_name: str) -> str:
    if native_name.startswith("luaL_"):
        return "cpkt_lua_l_" + native_name[len("luaL_"):]
    if native_name.startswith("luaopen_"):
        return "cpkt_lua_open_" + native_name[len("luaopen_"):]
    if native_name.startswith("lua_"):
        return "cpkt_lua_" + native_name[len("lua_"):]
    raise ValueError("unexpected Lua API name: " + native_name)


def symbols(command: Iterable[str], label: str) -> Set[str]:
    process = subprocess.run(list(command), check=False, text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if process.returncode != 0:
        raise ValueError(label + " failed: " + process.stderr.strip())
    values: Set[str] = set()
    for line in process.stdout.splitlines():
        fields = line.split()
        if fields:
            values.add(fields[-1].split("@", 1)[0])
    return values


def normalized_macho(values: Set[str]) -> Set[str]:
    return {value[1:] if value.startswith("_") else value for value in values}


def native_symbols(symbol_tool: str, library: pathlib.Path,
                   symbol_format: str) -> Set[str]:
    if symbol_format == "elf":
        return symbols((symbol_tool, "-D", "--defined-only", str(library)),
                       "native Lua symbol inspection")
    return normalized_macho(symbols((symbol_tool, "-gU", str(library)),
                                    "native Lua symbol inspection"))


def facade_imports(symbol_tool: str, library: pathlib.Path,
                   symbol_format: str) -> Set[str]:
    if symbol_format == "elf":
        return symbols((symbol_tool, "-D", "--undefined-only", str(library)),
                       "Lua facade import inspection")
    return normalized_macho(symbols((symbol_tool, "-u", str(library)),
                                    "Lua facade import inspection"))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--include-dir", required=True, type=pathlib.Path)
    parser.add_argument("--native-library", required=True, type=pathlib.Path)
    parser.add_argument("--facade-library", required=True, type=pathlib.Path)
    parser.add_argument("--facade-header", required=True, type=pathlib.Path)
    parser.add_argument("--symbol-tool", required=True)
    parser.add_argument("--symbol-format", choices=("elf", "macho"),
                        required=True)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    header_text = read_headers(args.include_dir)
    native_functions = set(DECLARATION.findall(header_text))
    if len(native_functions) != 156:
        raise ValueError("expected 156 Lua public functions, found {}".format(
            len(native_functions)))
    if "extern const char lua_ident[];" not in header_text:
        raise ValueError("Lua public lua_ident declaration is missing")
    if not args.facade_header.is_file():
        raise ValueError("Lua facade header is missing: " +
                         str(args.facade_header))
    facade_functions = set(FACADE_DECLARATION.findall(
        args.facade_header.read_text("utf-8")))
    expected_facade = {public_name(name) for name in native_functions}
    missing_facade = sorted(expected_facade - facade_functions)
    if missing_facade:
        raise ValueError("Lua facade omits public functions: " +
                         ", ".join(missing_facade))
    facade_text = args.facade_header.read_text("utf-8")
    missing_convenience = []
    for native_name, replacements in CONVENIENCE_EQUIVALENTS.items():
        if not all(replacement in facade_text for replacement in replacements):
            missing_convenience.append(native_name)
    if missing_convenience:
        raise ValueError("Lua facade omits convenience equivalents: " +
                         ", ".join(missing_convenience))

    native_defined = native_symbols(args.symbol_tool, args.native_library,
                                    args.symbol_format)
    missing_native = sorted(native_functions - native_defined)
    if missing_native:
        raise ValueError("native Lua library omits declared functions: " +
                         ", ".join(missing_native))
    if "lua_ident" not in native_defined:
        raise ValueError("native Lua library omits lua_ident")

    imports = facade_imports(args.symbol_tool, args.facade_library,
                             args.symbol_format)
    lua_imports = {name for name in imports if name.startswith("lua")}
    allowed_imports = native_functions | {"lua_ident"}
    private_imports = sorted(lua_imports - allowed_imports)
    if private_imports:
        raise ValueError("Lua facade imports private Lua symbols: " +
                         ", ".join(private_imports))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({
        "facade_function_count": len(expected_facade),
        "facade_helper_function_count": len(facade_functions - expected_facade),
        "convenience_equivalent_count": len(CONVENIENCE_EQUIVALENTS),
        "native_public_function_count": len(native_functions),
        "native_import_count": len(lua_imports),
        "native_imports": sorted(lua_imports),
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print("generate_lua_api_inventory.py: " + str(error), file=sys.stderr)
        raise SystemExit(1)
