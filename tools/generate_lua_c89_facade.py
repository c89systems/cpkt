#!/usr/bin/env python3
"""Generate the strict-C89, 64-bit Lua 5.5 public API boundary.

Lua 5.5's configured headers expose ``long long`` as lua_Integer.  This
generator preserves the complete callable Lua, lauxlib, and lualib surface
without requiring that non-C89 type in downstream headers.  The facade uses
two 32-bit words for Lua integer values and forwards to the pinned native Lua
library inside the generated implementation.
"""

import argparse
import pathlib
import re
import sys
from typing import Iterable, List, Sequence, Tuple


DECLARATION = re.compile(
    r"\b(?:LUA_API|LUALIB_API|LUAMOD_API)\s+"
    r"(?P<result>.*?)\(\s*(?P<name>lua(?:L|open)?_[A-Za-z0-9_]+)\s*\)"
    r"\s*\((?P<parameters>.*?)\)\s*;", re.DOTALL)

TYPE_REPLACEMENTS: Tuple[Tuple[str, str], ...] = (
    ("luaL_Buffer", "cpkt_lua_buffer"),
    ("luaL_Stream", "cpkt_lua_stream"),
    ("luaL_Reg", "cpkt_lua_reg"),
    ("lua_State", "cpkt_lua_state"),
    ("lua_Debug", "cpkt_lua_debug"),
    ("lua_Number", "cpkt_lua_number"),
    ("lua_Integer", "cpkt_lua_integer"),
    ("lua_Unsigned", "cpkt_lua_unsigned"),
    ("lua_KContext", "cpkt_lua_kcontext"),
    ("lua_CFunction", "cpkt_lua_c_function"),
    ("lua_KFunction", "cpkt_lua_k_function"),
    ("lua_Reader", "cpkt_lua_reader"),
    ("lua_Writer", "cpkt_lua_writer"),
    ("lua_Alloc", "cpkt_lua_alloc"),
    ("lua_WarnFunction", "cpkt_lua_warn_function"),
    ("lua_Hook", "cpkt_lua_hook"),
)


def strip_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)


def normalize(text: str) -> str:
    return " ".join(text.split())


def public_name(native_name: str) -> str:
    if native_name.startswith("luaL_"):
        return "cpkt_lua_l_" + native_name[len("luaL_"):]
    if native_name.startswith("luaopen_"):
        return "cpkt_lua_open_" + native_name[len("luaopen_"):]
    if native_name.startswith("lua_"):
        return "cpkt_lua_" + native_name[len("lua_"):]
    raise ValueError("unexpected Lua API name: " + native_name)


def transform(text: str) -> str:
    for native, public in TYPE_REPLACEMENTS:
        text = re.sub(r"\b" + re.escape(native) + r"\b", public, text)
    return text


def split_parameters(parameters: str) -> Iterable[str]:
    if normalize(parameters) == "void":
        return []
    depth = 0
    result: List[str] = []
    current: List[str] = []
    for character in parameters:
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        if character == "," and depth == 0:
            result.append("".join(current).strip())
            current = []
        else:
            current.append(character)
    tail = "".join(current).strip()
    if tail:
        result.append(tail)
    return result


def parameter_type_and_name(parameter: str) -> Tuple[str, str]:
    callback = re.search(
        r"\(\s*\*\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\)", parameter)
    if callback:
        name = callback.group("name")
        return parameter.replace("*" + name, "*", 1), name
    match = re.match(r"(?P<type>.+?)(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
                     r"\s*(?P<array>\[\s*\])?\s*$",
                     parameter.strip(), re.DOTALL)
    if not match:
        raise ValueError("cannot determine Lua parameter name: " + parameter)
    array = match.group("array") or ""
    return match.group("type").rstrip() + array, match.group("name")


def declarations(paths: Sequence[pathlib.Path]) -> List[Tuple[str, str, str]]:
    values: List[Tuple[str, str, str]] = []
    for path in paths:
        text = strip_comments(path.read_text(encoding="utf-8"))
        for match in DECLARATION.finditer(text):
            values.append((match.group("result"), match.group("name"),
                           match.group("parameters")))
    names = [item[1] for item in values]
    if len(values) != 156 or len(set(names)) != 156:
        raise ValueError("expected 156 unique Lua public functions, found {}".
                         format(len(set(names))))
    return values


def native_argument(native_type: str, name: str) -> str:
    compact = normalize(native_type)
    if compact == "va_list" or compact.endswith("[]"):
        return name
    if compact == "lua_Integer":
        return "cpkt_lua_native_integer(" + name + ")"
    if compact == "lua_Unsigned":
        return "cpkt_lua_native_unsigned(" + name + ")"
    return "({}){}".format(compact, name)


def definitions(items: Sequence[Tuple[str, str, str]]) -> str:
    special = {"lua_pushfstring", "lua_pushvfstring", "lua_gc", "luaL_error"}
    result: List[str] = []
    for native_result_text, native_name, native_parameters_text in items:
        if native_name in special:
            continue
        native_result = normalize(native_result_text)
        native_parameters = native_parameters_text.strip()
        public_result = transform(native_result_text).strip()
        public_parameters = transform(native_parameters).strip()
        arguments: List[str] = []
        for parameter in split_parameters(native_parameters):
            if parameter == "...":
                raise ValueError("unexpected variadic Lua API: " + native_name)
            native_type, name = parameter_type_and_name(parameter)
            arguments.append(native_argument(native_type, name))
        call = native_name + "(" + ", ".join(arguments) + ")"
        definition = "CPKT_LUA_API {} {}({}) {{\n".format(
            public_result, public_name(native_name), public_parameters)
        if native_result == "void":
            definition += "  {};\n".format(call)
        elif native_result == "lua_Integer":
            definition += "  return cpkt_lua_integer_from_native({});\n".format(call)
        elif native_result == "lua_Unsigned":
            definition += "  return cpkt_lua_unsigned_from_native({});\n".format(call)
        else:
            definition += "  return ({}){};\n".format(public_result, call)
        definition += "}\n"
        result.append(definition)
    return "\n".join(result)


def convenience_header() -> str:
    return r"""/* C89 equivalents of every documented Lua convenience macro. */
#define CPKT_LUA_SIGNATURE "\x1bLua"
#define CPKT_LUA_VERSION_MAJOR "5"
#define CPKT_LUA_VERSION_MINOR "5"
#define CPKT_LUA_VERSION_RELEASE "1"
#define CPKT_LUA_VERSION "Lua 5.5"
#define CPKT_LUA_RELEASE "Lua 5.5.1"
#define CPKT_LUA_COPYRIGHT CPKT_LUA_RELEASE "  Copyright (C) 1994-2026 Lua.org, PUC-Rio"
#define CPKT_LUA_AUTHORS "R. Ierusalimschy, L. H. de Figueiredo, W. Celes"
#define CPKT_LUA_OPADD 0
#define CPKT_LUA_OPSUB 1
#define CPKT_LUA_OPMUL 2
#define CPKT_LUA_OPMOD 3
#define CPKT_LUA_OPPOW 4
#define CPKT_LUA_OPDIV 5
#define CPKT_LUA_OPIDIV 6
#define CPKT_LUA_OPBAND 7
#define CPKT_LUA_OPBOR 8
#define CPKT_LUA_OPBXOR 9
#define CPKT_LUA_OPSHL 10
#define CPKT_LUA_OPSHR 11
#define CPKT_LUA_OPUNM 12
#define CPKT_LUA_OPBNOT 13
#define CPKT_LUA_OPEQ 0
#define CPKT_LUA_OPLT 1
#define CPKT_LUA_OPLE 2
#define CPKT_LUA_GCPMINORMUL 0
#define CPKT_LUA_GCPMAJORMINOR 1
#define CPKT_LUA_GCPMINORMAJOR 2
#define CPKT_LUA_GCPPAUSE 3
#define CPKT_LUA_GCPSTEPMUL 4
#define CPKT_LUA_GCPSTEPSIZE 5
#define CPKT_LUA_GCPN 6
#define CPKT_LUA_N2SBUFFSZ 64
#define CPKT_LUA_HOOKCALL 0
#define CPKT_LUA_HOOKRET 1
#define CPKT_LUA_HOOKLINE 2
#define CPKT_LUA_HOOKCOUNT 3
#define CPKT_LUA_HOOKTAILCALL 4
#define CPKT_LUA_MASKCALL (1 << CPKT_LUA_HOOKCALL)
#define CPKT_LUA_MASKRET (1 << CPKT_LUA_HOOKRET)
#define CPKT_LUA_MASKLINE (1 << CPKT_LUA_HOOKLINE)
#define CPKT_LUA_MASKCOUNT (1 << CPKT_LUA_HOOKCOUNT)
#define CPKT_LUA_GNAME "_G"
#define CPKT_LUA_ERRFILE (CPKT_LUA_ERRERR + 1)
#define CPKT_LUA_LOADED_TABLE "_LOADED"
#define CPKT_LUA_PRELOAD_TABLE "_PRELOAD"
#define CPKT_LUA_NOREF (-2)
#define CPKT_LUA_REFNIL (-1)
#define CPKT_LUA_FILEHANDLE "FILE*"
#define CPKT_LUA_GLIBK 1
#define CPKT_LUA_LOADLIBNAME "package"
#define CPKT_LUA_LOADLIBK (CPKT_LUA_GLIBK << 1)
#define CPKT_LUA_COLIBNAME "coroutine"
#define CPKT_LUA_COLIBK (CPKT_LUA_LOADLIBK << 1)
#define CPKT_LUA_DBLIBNAME "debug"
#define CPKT_LUA_DBLIBK (CPKT_LUA_COLIBK << 1)
#define CPKT_LUA_IOLIBNAME "io"
#define CPKT_LUA_IOLIBK (CPKT_LUA_DBLIBK << 1)
#define CPKT_LUA_MATHLIBNAME "math"
#define CPKT_LUA_MATHLIBK (CPKT_LUA_IOLIBK << 1)
#define CPKT_LUA_OSLIBNAME "os"
#define CPKT_LUA_OSLIBK (CPKT_LUA_MATHLIBK << 1)
#define CPKT_LUA_STRLIBNAME "string"
#define CPKT_LUA_STRLIBK (CPKT_LUA_OSLIBK << 1)
#define CPKT_LUA_TABLIBNAME "table"
#define CPKT_LUA_TABLIBK (CPKT_LUA_STRLIBK << 1)
#define CPKT_LUA_UTF8LIBNAME "utf8"
#define CPKT_LUA_UTF8LIBK (CPKT_LUA_TABLIBK << 1)
#define CPKT_LUA_VERSUFFIX "_5_5"
#define CPKT_LUA_L_NUMSIZES (sizeof(cpkt_lua_integer) * 16 + sizeof(cpkt_lua_number))
#define CPKT_LUA_MAX_INTEGER cpkt_lua_integer_make(0x7fffffffU, 0xffffffffU)
#define CPKT_LUA_MIN_INTEGER cpkt_lua_integer_make(0x80000000U, 0U)
#define CPKT_LUA_MAX_UNSIGNED cpkt_lua_unsigned_make(0xffffffffU, 0xffffffffU)

/** Raises a Lua error if the loaded Lua ABI does not match this facade. */
CPKT_LUA_API void cpkt_lua_l_checkversion(cpkt_lua_state *state);
/** Appends one character to an auxiliary string buffer. */
CPKT_LUA_API void cpkt_lua_l_addchar(cpkt_lua_buffer *buffer_ptr,
                                     char character);
/** Adds two exact two-word Lua integers. */
CPKT_LUA_API cpkt_lua_integer cpkt_lua_l_integer_add(cpkt_lua_integer left,
                                                       cpkt_lua_integer right);
/** Subtracts two exact two-word Lua integers. */
CPKT_LUA_API cpkt_lua_integer cpkt_lua_l_integer_subtract(cpkt_lua_integer left,
                                                            cpkt_lua_integer right);
/** Multiplies two exact two-word Lua integers. */
CPKT_LUA_API cpkt_lua_integer cpkt_lua_l_integer_multiply(cpkt_lua_integer left,
                                                            cpkt_lua_integer right);
/** Computes bitwise AND of two Lua integers. */
CPKT_LUA_API cpkt_lua_integer cpkt_lua_l_integer_bit_and(cpkt_lua_integer left,
                                                           cpkt_lua_integer right);
/** Computes bitwise OR of two Lua integers. */
CPKT_LUA_API cpkt_lua_integer cpkt_lua_l_integer_bit_or(cpkt_lua_integer left,
                                                          cpkt_lua_integer right);
/** Computes bitwise XOR of two Lua integers. */
CPKT_LUA_API cpkt_lua_integer cpkt_lua_l_integer_bit_xor(cpkt_lua_integer left,
                                                           cpkt_lua_integer right);
/** Shifts a Lua integer left by count bits. */
CPKT_LUA_API cpkt_lua_integer cpkt_lua_l_integer_shift_left(cpkt_lua_integer value,
                                                              unsigned int count);
/** Shifts a Lua integer right by count bits. */
CPKT_LUA_API cpkt_lua_integer cpkt_lua_l_integer_shift_right(cpkt_lua_integer value,
                                                               unsigned int count);

#define cpkt_lua_upvalueindex(index) (CPKT_LUA_REGISTRYINDEX - (index))
#define cpkt_lua_call(state, arguments, results) \
  cpkt_lua_callk((state), (arguments), (results), (cpkt_lua_kcontext)0, 0)
#define cpkt_lua_pcall(state, arguments, results, error_function) \
  cpkt_lua_pcallk((state), (arguments), (results), (error_function), \
                  (cpkt_lua_kcontext)0, 0)
#define cpkt_lua_yield(state, results) \
  cpkt_lua_yieldk((state), (results), (cpkt_lua_kcontext)0, 0)
#define cpkt_lua_getextraspace(state) ((void *)((char *)(state) - sizeof(void *)))
#define cpkt_lua_tonumber(state, index) cpkt_lua_tonumberx((state), (index), 0)
#define cpkt_lua_tointeger(state, index) cpkt_lua_tointegerx((state), (index), 0)
#define cpkt_lua_pop(state, count) cpkt_lua_settop((state), -(count) - 1)
#define cpkt_lua_newtable(state) cpkt_lua_createtable((state), 0, 0)
#define cpkt_lua_pushcfunction(state, function) \
  cpkt_lua_pushcclosure((state), (function), 0)
#define cpkt_lua_register(state, name, function) \
  (cpkt_lua_pushcfunction((state), (function)), cpkt_lua_setglobal((state), (name)))
#define cpkt_lua_isfunction(state, index) \
  (cpkt_lua_type((state), (index)) == CPKT_LUA_TFUNCTION)
#define cpkt_lua_istable(state, index) \
  (cpkt_lua_type((state), (index)) == CPKT_LUA_TTABLE)
#define cpkt_lua_islightuserdata(state, index) \
  (cpkt_lua_type((state), (index)) == CPKT_LUA_TLIGHTUSERDATA)
#define cpkt_lua_isnil(state, index) \
  (cpkt_lua_type((state), (index)) == CPKT_LUA_TNIL)
#define cpkt_lua_isboolean(state, index) \
  (cpkt_lua_type((state), (index)) == CPKT_LUA_TBOOLEAN)
#define cpkt_lua_isthread(state, index) \
  (cpkt_lua_type((state), (index)) == CPKT_LUA_TTHREAD)
#define cpkt_lua_isnone(state, index) \
  (cpkt_lua_type((state), (index)) == CPKT_LUA_TNONE)
#define cpkt_lua_isnoneornil(state, index) (cpkt_lua_type((state), (index)) <= 0)
#define cpkt_lua_pushliteral(state, string) cpkt_lua_pushstring((state), "" string)
#define cpkt_lua_pushglobaltable(state) \
  ((void)cpkt_lua_rawgeti((state), CPKT_LUA_REGISTRYINDEX, \
                           cpkt_lua_integer_make(0U, CPKT_LUA_RIDX_GLOBALS)))
#define cpkt_lua_tostring(state, index) cpkt_lua_tolstring((state), (index), 0)
#define cpkt_lua_insert(state, index) cpkt_lua_rotate((state), (index), 1)
#define cpkt_lua_remove(state, index) \
  (cpkt_lua_rotate((state), (index), -1), cpkt_lua_pop((state), 1))
#define cpkt_lua_replace(state, index) \
  (cpkt_lua_copy((state), -1, (index)), cpkt_lua_pop((state), 1))
#define cpkt_lua_newuserdata(state, size) cpkt_lua_newuserdatauv((state), (size), 1)
#define cpkt_lua_getuservalue(state, index) cpkt_lua_getiuservalue((state), (index), 1)
#define cpkt_lua_setuservalue(state, index) cpkt_lua_setiuservalue((state), (index), 1)
#define cpkt_lua_resetthread(state) cpkt_lua_closethread((state), 0)
#define cpkt_lua_l_loadfile(state, filename) \
  cpkt_lua_l_loadfilex((state), (filename), 0)
#define cpkt_lua_l_loadbuffer(state, buffer, size, name) \
  cpkt_lua_l_loadbufferx((state), (buffer), (size), (name), 0)
#define cpkt_lua_l_checkstring(state, argument) \
  cpkt_lua_l_checklstring((state), (argument), 0)
#define cpkt_lua_l_optstring(state, argument, default_value) \
  cpkt_lua_l_optlstring((state), (argument), (default_value), 0)
#define cpkt_lua_l_typename(state, index) \
  cpkt_lua_typename((state), cpkt_lua_type((state), (index)))
#define cpkt_lua_l_dofile(state, filename) \
  (cpkt_lua_l_loadfile((state), (filename)) || \
   cpkt_lua_pcall((state), 0, CPKT_LUA_MULTRET, 0))
#define cpkt_lua_l_dostring(state, string) \
  (cpkt_lua_l_loadstring((state), (string)) || \
   cpkt_lua_pcall((state), 0, CPKT_LUA_MULTRET, 0))
#define cpkt_lua_l_getmetatable(state, name) \
  cpkt_lua_getfield((state), CPKT_LUA_REGISTRYINDEX, (name))
#define cpkt_lua_l_opt(state, function, argument, default_value) \
  (cpkt_lua_isnoneornil((state), (argument)) ? (default_value) : \
   (function)((state), (argument)))
#define cpkt_lua_l_newlibtable(state, registration) \
  cpkt_lua_createtable((state), 0, \
                       (int)(sizeof(registration) / sizeof((registration)[0]) - 1))
#define cpkt_lua_l_newlib(state, registration) \
  (cpkt_lua_l_checkversion((state)), cpkt_lua_l_newlibtable((state), (registration)), \
   cpkt_lua_l_setfuncs((state), (registration), 0))
#define cpkt_lua_l_argcheck(state, condition, argument, message) \
  ((void)((condition) || cpkt_lua_l_argerror((state), (argument), (message))))
#define cpkt_lua_l_argexpected(state, condition, argument, type_name) \
  ((void)((condition) || cpkt_lua_l_typeerror((state), (argument), (type_name))))
#define cpkt_lua_l_pushfail(state) cpkt_lua_pushnil((state))
#define cpkt_lua_l_bufflen(buffer_ptr) ((buffer_ptr)->n)
#define cpkt_lua_l_buffaddr(buffer_ptr) ((buffer_ptr)->b)
#define cpkt_lua_l_addsize(buffer_ptr, size) ((buffer_ptr)->n += (size))
#define cpkt_lua_l_buffsub(buffer_ptr, size) ((buffer_ptr)->n -= (size))
#define cpkt_lua_l_prepbuffer(buffer_ptr) \
  cpkt_lua_l_prepbuffsize((buffer_ptr), CPKT_LUA_BUFFER_SIZE)
#define cpkt_lua_l_openlibs(state) cpkt_lua_l_openselectedlibs((state), ~0, 0)
"""


def header(items: Sequence[Tuple[str, str, str]]) -> str:
    prototypes: List[str] = []
    contracts = {
        "lua_newstate": "Creates an owned Lua state with the supplied allocator; close it with cpkt_lua_close().",
        "lua_close": "Closes a Lua state and releases its Lua-owned objects; the pointer becomes invalid.",
        "lua_newthread": "Pushes a new coroutine on the parent stack; it shares the parent's global state.",
        "lua_gettop": "Returns the number of values on the Lua stack.",
        "lua_settop": "Sets stack height; a negative index is relative to the stack top.",
        "lua_tointegerx": "Converts a stack value to an exact two-word integer; isnum reports conversion success.",
        "lua_tolstring": "Returns a Lua-owned string view and optionally its byte length; stack changes can invalidate it.",
        "lua_pushlstring": "Copies len bytes onto the Lua stack as a string; the returned view is Lua-owned.",
        "lua_pushexternalstring": "Pushes an external string; keep its NUL-terminated buffer unchanged while Lua uses it. Lua calls a supplied finalizer after use.",
        "lua_callk": "Calls a Lua function on the stack; errors may long-jump through the caller.",
        "lua_pcallk": "Calls a Lua function with protected error handling and a status result.",
        "lua_load": "Loads a chunk from a reader callback and leaves the compiled function or error on the stack.",
        "lua_dump": "Writes a compiled function through the writer callback; it does not pop the function.",
        "lua_gc": "Controls Lua garbage collection; option-specific arguments follow the Lua 5.5 API.",
        "luaL_newstate": "Creates an owned Lua state with the standard allocator; close it with cpkt_lua_close().",
        "luaL_loadfilex": "Loads a file as a chunk without running it; returns a Lua status code.",
        "luaL_loadbufferx": "Loads a byte buffer as a chunk without running it; returns a Lua status code.",
        "luaL_ref": "Stores the stack-top value in a table and returns a registry reference.",
        "luaL_unref": "Releases a registry reference in the given table.",
        "luaL_openselectedlibs": "Loads or preloads the selected standard libraries according to the bitmasks.",
    }
    for result, name, parameters in items:
        public_result = transform(result).strip()
        public_parameters = transform(parameters).strip()
        description = contracts.get(name, "Uses the Lua 5.5 C API contract for {}.".format(name))
        anchor = name
        if name.startswith("luaopen_"):
            description = "Opens the {} standard library in the given state.".format(
                name[len("luaopen_"):])
            anchor = "6.1"
        elif name == "luaL_checkversion_":
            description = "Checks the Lua ABI version and numeric type sizes; raises a Lua error on mismatch."
            anchor = "luaL_checkversion"
        prototypes.append("/** {}\n * @see https://www.lua.org/manual/5.5/manual.html#{}\n */".format(
            description, anchor))
        prototypes.append("CPKT_LUA_API {} {}({});".format(
            public_result, public_name(name), public_parameters))
    return """/* Generated by tools/generate_lua_c89_facade.py; do not edit. */
#ifndef CPKT_LUA_H
#define CPKT_LUA_H

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#if defined(_WIN32) && defined(CPKT_LUA_BUILDING_SHARED)
#define CPKT_LUA_API __declspec(dllexport)
#elif defined(_WIN32) && !defined(CPKT_LUA_STATIC)
#define CPKT_LUA_API __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#define CPKT_LUA_API __attribute__((visibility("default")))
#else
#define CPKT_LUA_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CPKT_LUA_VERSION_MAJOR_N 5
#define CPKT_LUA_VERSION_MINOR_N 5
#define CPKT_LUA_VERSION_RELEASE_N 1
#define CPKT_LUA_VERSION_NUM 505
#define CPKT_LUA_VERSION_RELEASE_NUM 50501
#define CPKT_LUA_ABI_VERSION 0
#define CPKT_LUA_MULTRET (-1)
#define CPKT_LUA_REGISTRYINDEX (-(INT_MAX / 2 + 1000))
#define CPKT_LUA_UPVALUEINDEX(index) (CPKT_LUA_REGISTRYINDEX - (index))
#define CPKT_LUA_OK 0
#define CPKT_LUA_YIELD 1
#define CPKT_LUA_ERRRUN 2
#define CPKT_LUA_ERRSYNTAX 3
#define CPKT_LUA_ERRMEM 4
#define CPKT_LUA_ERRERR 5
#define CPKT_LUA_TNONE (-1)
#define CPKT_LUA_TNIL 0
#define CPKT_LUA_TBOOLEAN 1
#define CPKT_LUA_TLIGHTUSERDATA 2
#define CPKT_LUA_TNUMBER 3
#define CPKT_LUA_TSTRING 4
#define CPKT_LUA_TTABLE 5
#define CPKT_LUA_TFUNCTION 6
#define CPKT_LUA_TUSERDATA 7
#define CPKT_LUA_TTHREAD 8
#define CPKT_LUA_NUMTYPES 9
#define CPKT_LUA_MINSTACK 20
#define CPKT_LUA_RIDX_GLOBALS 2
#define CPKT_LUA_RIDX_MAINTHREAD 3
#define CPKT_LUA_RIDX_LAST 3
#define CPKT_LUA_GCSTOP 0
#define CPKT_LUA_GCRESTART 1
#define CPKT_LUA_GCCOLLECT 2
#define CPKT_LUA_GCCOUNT 3
#define CPKT_LUA_GCCOUNTB 4
#define CPKT_LUA_GCSTEP 5
#define CPKT_LUA_GCISRUNNING 6
#define CPKT_LUA_GCGEN 7
#define CPKT_LUA_GCINC 8
#define CPKT_LUA_GCPARAM 9
#define CPKT_LUA_IDSIZE 60
#define CPKT_LUA_BUFFER_SIZE (16 * sizeof(void *) * sizeof(double))

/** Opaque Lua VM state; close the owner with cpkt_lua_close(). */
typedef struct cpkt_lua_state cpkt_lua_state;
/** Lua numeric value; this bundle configures it as double. */
typedef double cpkt_lua_number;
/** Opaque C continuation context passed unchanged to its callback. */
typedef ptrdiff_t cpkt_lua_kcontext;
/** Exact 64-bit Lua integer bits as high and low 32-bit words. */
typedef struct cpkt_lua_integer {
  /** Most significant 32 bits, including the signed value's sign bit. */
  unsigned int high;
  /** Least significant 32 bits. */
  unsigned int low;
} cpkt_lua_integer;
/** Unsigned 64-bit Lua integer bits in the same two-word layout. */
typedef cpkt_lua_integer cpkt_lua_unsigned;
/** Lua C function; return the number of results left on the stack. */
typedef int (*cpkt_lua_c_function)(cpkt_lua_state *state);
/** Continuation callback for a yielded protected call or call. */
typedef int (*cpkt_lua_k_function)(cpkt_lua_state *state, int status,
                                   cpkt_lua_kcontext context);
/** Return a borrowed chunk valid until the next reader call; NULL or zero
 * size signals end of input. */
typedef const char *(*cpkt_lua_reader)(cpkt_lua_state *state, void *user,
                                       size_t *size);
/** Return zero on successful consumption of a chunk, nonzero on failure. */
typedef int (*cpkt_lua_writer)(cpkt_lua_state *state, const void *data,
                               size_t size, void *user);
/** Lua allocator: a zero new_size frees pointer; NULL signals failure. */
typedef void *(*cpkt_lua_alloc)(void *user, void *pointer, size_t old_size,
                                size_t new_size);
/** Receives warning fragments; to_continue marks an unfinished message. */
typedef void (*cpkt_lua_warn_function)(void *user, const char *message,
                                       int to_continue);

/** Hook/debug record; string fields are Lua-owned views. */
typedef struct cpkt_lua_debug {
  int event;
  const char *name;
  const char *namewhat;
  const char *what;
  const char *source;
  size_t srclen;
  int currentline;
  int linedefined;
  int lastlinedefined;
  unsigned char nups;
  unsigned char nparams;
  char isvararg;
  unsigned char extraargs;
  char istailcall;
  int ftransfer;
  int ntransfer;
  char short_src[CPKT_LUA_IDSIZE];
  void *i_ci;
} cpkt_lua_debug;
/** Lua debug hook; record is borrowed for this callback invocation. */
typedef void (*cpkt_lua_hook)(cpkt_lua_state *state, cpkt_lua_debug *record);

/** Name/function record; registration arrays end with a NULL name. */
typedef struct cpkt_lua_reg {
  const char *name;
  cpkt_lua_c_function func;
} cpkt_lua_reg;
/** Auxiliary string builder; finish with a cpkt_lua_l_pushresult* operation. */
typedef struct cpkt_lua_buffer {
  char *b;
  size_t size;
  size_t n;
  cpkt_lua_state *L;
  union {
    long double alignment;
    void *pointer;
    char bytes[CPKT_LUA_BUFFER_SIZE];
  } init;
} cpkt_lua_buffer;
/** File stream record used by Lua's standard I/O library. */
typedef struct cpkt_lua_stream {
  FILE *f;
  cpkt_lua_c_function closef;
} cpkt_lua_stream;

/** Packs exact 64-bit two's-complement bits from high and low words. */
CPKT_LUA_API cpkt_lua_integer cpkt_lua_integer_make(unsigned int high,
                                                     unsigned int low);
/** Returns the high 32 bits of a Lua integer. */
CPKT_LUA_API unsigned int cpkt_lua_integer_high(cpkt_lua_integer value);
/** Returns the low 32 bits of a Lua integer. */
CPKT_LUA_API unsigned int cpkt_lua_integer_low(cpkt_lua_integer value);
/** Packs the high and low words of an unsigned Lua integer. */
CPKT_LUA_API cpkt_lua_unsigned cpkt_lua_unsigned_make(unsigned int high,
                                                       unsigned int low);
/** Returns a static build-identification string; do not free it. */
CPKT_LUA_API const char *cpkt_lua_ident(void);
/** Formats a string and pushes it on the Lua stack. */
CPKT_LUA_API const char *cpkt_lua_pushfstring(cpkt_lua_state *state,
                                              const char *format, ...);
/** Controls Lua garbage collection; option-specific arguments follow Lua 5.5. */
CPKT_LUA_API int cpkt_lua_gc(cpkt_lua_state *state, int option, ...);
/** Raises a formatted Lua error; does not return normally. */
CPKT_LUA_API int cpkt_lua_l_error(cpkt_lua_state *state,
                                  const char *format, ...);

__CPKT_LUA_PROTOTYPES__

__CPKT_LUA_CONVENIENCE__

#ifdef __cplusplus
}
#endif

#endif /* CPKT_LUA_H */
""".replace("__CPKT_LUA_PROTOTYPES__", "\n".join(prototypes)).replace(
        "__CPKT_LUA_CONVENIENCE__", convenience_header())


def special_definitions() -> str:
    return """CPKT_LUA_API cpkt_lua_integer
cpkt_lua_integer_make(unsigned int high, unsigned int low)
{
  cpkt_lua_integer value;

  value.high = high;
  value.low = low;
  return value;
}

CPKT_LUA_API unsigned int cpkt_lua_integer_high(cpkt_lua_integer value)
{
  return value.high;
}

CPKT_LUA_API unsigned int cpkt_lua_integer_low(cpkt_lua_integer value)
{
  return value.low;
}

CPKT_LUA_API cpkt_lua_unsigned
cpkt_lua_unsigned_make(unsigned int high, unsigned int low)
{
  return cpkt_lua_integer_make(high, low);
}

CPKT_LUA_API const char *cpkt_lua_ident(void)
{
  return lua_ident;
}

static uint64_t cpkt_lua_integer_bits(cpkt_lua_integer value);
static lua_Integer cpkt_lua_native_integer(cpkt_lua_integer value);
typedef struct cpkt_lua_format_call {
  const char *format;
  va_list *arguments;
} cpkt_lua_format_call;
static const char *cpkt_lua_push_format_converted(lua_State *native_state,
                                                  const char *format,
                                                  va_list arguments);
static int cpkt_lua_push_format_protected(lua_State *native_state);
static const char *cpkt_lua_push_format(cpkt_lua_state *state,
                                         const char *format,
                                         va_list arguments);
static const char *cpkt_lua_push_memory_message(lua_State *native_state,
                                                 const char *format, ...);

CPKT_LUA_API const char *
cpkt_lua_pushfstring(cpkt_lua_state *state, const char *format, ...)
{
  va_list arguments;
  const char *result;

  va_start(arguments, format);
  result = cpkt_lua_push_format(state, format, arguments);
  va_end(arguments);
  if (result == NULL) {
    lua_pushstring((lua_State *)state, "not enough memory");
    lua_error((lua_State *)state);
  }
  return result;
}

CPKT_LUA_API const char *
cpkt_lua_pushvfstring(cpkt_lua_state *state, const char *format,
                      va_list arguments)
{
  return cpkt_lua_push_format(state, format, arguments);
}

static const char *
cpkt_lua_push_format(cpkt_lua_state *state, const char *format,
                     va_list arguments)
{
  lua_State *native_state;
  cpkt_lua_format_call call;
  va_list copied_arguments;
  int status;

  native_state = (lua_State *)state;
  if (strstr(format, "%I") == NULL && strstr(format, "%U") == NULL)
    return lua_pushvfstring(native_state, format, arguments);
  if (!lua_checkstack(native_state, 2)) {
    (void)cpkt_lua_push_memory_message(native_state, "%s", "not enough memory");
    return NULL;
  }
  call.format = format;
  __builtin_va_copy(copied_arguments, arguments);
  call.arguments = &copied_arguments;
  lua_pushcfunction(native_state, cpkt_lua_push_format_protected);
  lua_pushlightuserdata(native_state, &call);
  status = lua_pcall(native_state, 1, 1, 0);
  va_end(copied_arguments);
  if (status != LUA_OK)
    return NULL;
  return lua_tolstring(native_state, -1, NULL);
}

static const char *cpkt_lua_push_memory_message(lua_State *native_state,
                                                 const char *format, ...)
{
  va_list arguments;
  const char *message;

  va_start(arguments, format);
  message = lua_pushvfstring(native_state, format, arguments);
  va_end(arguments);
  return message;
}

static int cpkt_lua_push_format_protected(lua_State *native_state)
{
  cpkt_lua_format_call *call;

  call = (cpkt_lua_format_call *)lua_touserdata(native_state, 1);
  (void)cpkt_lua_push_format_converted(native_state, call->format,
                                       *call->arguments);
  return 1;
}

static const char *
cpkt_lua_push_format_converted(lua_State *native_state, const char *format,
                               va_list arguments)
{
  luaL_Buffer buffer;
  const char *cursor;

  luaL_buffinit(native_state, &buffer);
  cursor = format;
  while (*cursor != '\\0') {
    const char *percent = strchr(cursor, '%');
    if (percent == NULL) {
      luaL_addlstring(&buffer, cursor, strlen(cursor));
      break;
    }
    luaL_addlstring(&buffer, cursor, (size_t)(percent - cursor));
    cursor = percent + 1;
    if (*cursor == '\\0') {
      luaL_addchar(&buffer, '%');
      break;
    }
    switch (*cursor) {
      case '%': luaL_addchar(&buffer, '%'); break;
      case 's': lua_pushfstring(native_state, "%s", va_arg(arguments, char *)); break;
      case 'c': lua_pushfstring(native_state, "%c", va_arg(arguments, int)); break;
      case 'd': lua_pushfstring(native_state, "%d", va_arg(arguments, int)); break;
      case 'f': lua_pushfstring(native_state, "%f", va_arg(arguments, double)); break;
      case 'p': lua_pushfstring(native_state, "%p", va_arg(arguments, void *)); break;
      case 'I': lua_pushfstring(native_state, "%I", cpkt_lua_native_integer(va_arg(arguments, cpkt_lua_integer))); break;
      case 'U': lua_pushfstring(native_state, "%U",
                                va_arg(arguments, unsigned long)); break;
      default:
        luaL_addchar(&buffer, '%');
        luaL_addchar(&buffer, *cursor);
        ++cursor;
        continue;
    }
    if (*cursor != '%')
      luaL_addvalue(&buffer);
    ++cursor;
  }
  luaL_pushresult(&buffer);
  return lua_tolstring(native_state, -1, NULL);
}

CPKT_LUA_API int cpkt_lua_l_error(cpkt_lua_state *state,
                                  const char *format, ...)
{
  va_list arguments;

  luaL_where((lua_State *)state, 1);
  va_start(arguments, format);
  (void)cpkt_lua_push_format(state, format, arguments);
  va_end(arguments);
  lua_concat((lua_State *)state, 2);
  return lua_error((lua_State *)state);
}

CPKT_LUA_API int cpkt_lua_gc(cpkt_lua_state *state, int option, ...)
{
  va_list arguments;
  int first;
  int second;
  size_t step_size;

  va_start(arguments, option);
  if (option == LUA_GCSTEP) {
    step_size = va_arg(arguments, size_t);
    va_end(arguments);
    return lua_gc((lua_State *)state, option, step_size);
  }
  if (option == LUA_GCGEN) {
    va_end(arguments);
    return lua_gc((lua_State *)state, option);
  }
  if (option == LUA_GCINC) {
    va_end(arguments);
    return lua_gc((lua_State *)state, option);
  }
  if (option == LUA_GCPARAM) {
    first = va_arg(arguments, int);
    second = va_arg(arguments, int);
    va_end(arguments);
    return lua_gc((lua_State *)state, option, first, second);
  }
  va_end(arguments);
  return lua_gc((lua_State *)state, option);
}

CPKT_LUA_API void cpkt_lua_l_checkversion(cpkt_lua_state *state)
{
  luaL_checkversion_((lua_State *)state, LUA_VERSION_NUM, LUAL_NUMSIZES);
}

CPKT_LUA_API void
cpkt_lua_l_addchar(cpkt_lua_buffer *buffer_ptr, char character)
{
  if (buffer_ptr->n >= buffer_ptr->size) {
    (void)cpkt_lua_l_prepbuffsize(buffer_ptr, 1);
  }
  buffer_ptr->b[buffer_ptr->n++] = character;
}

CPKT_LUA_API cpkt_lua_integer
cpkt_lua_l_integer_add(cpkt_lua_integer left, cpkt_lua_integer right)
{
  return cpkt_lua_integer_from_bits(cpkt_lua_integer_bits(left) +
                                    cpkt_lua_integer_bits(right));
}

CPKT_LUA_API cpkt_lua_integer
cpkt_lua_l_integer_subtract(cpkt_lua_integer left, cpkt_lua_integer right)
{
  return cpkt_lua_integer_from_bits(cpkt_lua_integer_bits(left) -
                                    cpkt_lua_integer_bits(right));
}

CPKT_LUA_API cpkt_lua_integer
cpkt_lua_l_integer_multiply(cpkt_lua_integer left, cpkt_lua_integer right)
{
  return cpkt_lua_integer_from_bits(cpkt_lua_integer_bits(left) *
                                    cpkt_lua_integer_bits(right));
}

CPKT_LUA_API cpkt_lua_integer
cpkt_lua_l_integer_bit_and(cpkt_lua_integer left, cpkt_lua_integer right)
{
  return cpkt_lua_integer_from_bits(cpkt_lua_integer_bits(left) &
                                    cpkt_lua_integer_bits(right));
}

CPKT_LUA_API cpkt_lua_integer
cpkt_lua_l_integer_bit_or(cpkt_lua_integer left, cpkt_lua_integer right)
{
  return cpkt_lua_integer_from_bits(cpkt_lua_integer_bits(left) |
                                    cpkt_lua_integer_bits(right));
}

CPKT_LUA_API cpkt_lua_integer
cpkt_lua_l_integer_bit_xor(cpkt_lua_integer left, cpkt_lua_integer right)
{
  return cpkt_lua_integer_from_bits(cpkt_lua_integer_bits(left) ^
                                    cpkt_lua_integer_bits(right));
}

CPKT_LUA_API cpkt_lua_integer
cpkt_lua_l_integer_shift_left(cpkt_lua_integer value, unsigned int count)
{
  if (count >= 64U) {
    return cpkt_lua_integer_make(0U, 0U);
  }
  return cpkt_lua_integer_from_bits(cpkt_lua_integer_bits(value) << count);
}

CPKT_LUA_API cpkt_lua_integer
cpkt_lua_l_integer_shift_right(cpkt_lua_integer value, unsigned int count)
{
  if (count >= 64U) {
    return cpkt_lua_integer_make(0U, 0U);
  }
  return cpkt_lua_integer_from_bits(cpkt_lua_integer_bits(value) >> count);
}
"""


def source(items: Sequence[Tuple[str, str, str]]) -> str:
    return """/* Generated by tools/generate_lua_c89_facade.py; do not edit. */
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <cpkt/lua.h>

typedef char cpkt_lua_unsigned_int_is_32_bits[
    (sizeof(unsigned int) * CHAR_BIT == 32) ? 1 : -1];
typedef char cpkt_lua_native_integer_is_64_bits[
    (sizeof(lua_Integer) * CHAR_BIT == 64) ? 1 : -1];
typedef char cpkt_lua_debug_layout_matches_native[
    (sizeof(cpkt_lua_debug) == sizeof(lua_Debug)) ? 1 : -1];
typedef char cpkt_lua_buffer_layout_matches_native[
    (sizeof(cpkt_lua_buffer) == sizeof(luaL_Buffer)) ? 1 : -1];
typedef char cpkt_lua_stream_layout_matches_native[
    (sizeof(cpkt_lua_stream) == sizeof(luaL_Stream)) ? 1 : -1];
typedef char cpkt_lua_reg_layout_matches_native[
    (sizeof(cpkt_lua_reg) == sizeof(luaL_Reg)) ? 1 : -1];

#define CPKT_LUA_ASSERT_LAYOUT_FIELD(public_type, native_type, field) \\
  typedef char cpkt_lua_layout_##public_type##_##field[ \\
      (offsetof(public_type, field) == offsetof(native_type, field)) ? 1 : -1]

CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, event);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, name);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, namewhat);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, what);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, source);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, srclen);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, currentline);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, linedefined);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, lastlinedefined);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, nups);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, nparams);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, isvararg);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, extraargs);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, istailcall);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, ftransfer);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, ntransfer);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, short_src);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_debug, lua_Debug, i_ci);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_buffer, luaL_Buffer, b);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_buffer, luaL_Buffer, size);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_buffer, luaL_Buffer, n);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_buffer, luaL_Buffer, L);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_buffer, luaL_Buffer, init);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_stream, luaL_Stream, f);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_stream, luaL_Stream, closef);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_reg, luaL_Reg, name);
CPKT_LUA_ASSERT_LAYOUT_FIELD(cpkt_lua_reg, luaL_Reg, func);

#undef CPKT_LUA_ASSERT_LAYOUT_FIELD

static uint64_t cpkt_lua_integer_bits(cpkt_lua_integer value)
{
  return ((uint64_t)value.high << 32) | (uint64_t)value.low;
}

static cpkt_lua_integer cpkt_lua_integer_from_bits(uint64_t bits)
{
  return cpkt_lua_integer_make((unsigned int)(bits >> 32),
                               (unsigned int)bits);
}

static lua_Integer cpkt_lua_native_integer(cpkt_lua_integer value)
{
  return (lua_Integer)cpkt_lua_integer_bits(value);
}

static cpkt_lua_integer cpkt_lua_integer_from_native(lua_Integer value)
{
  return cpkt_lua_integer_from_bits((uint64_t)value);
}

static cpkt_lua_unsigned cpkt_lua_unsigned_from_native(lua_Unsigned value)
{
  uint64_t bits;

  bits = (uint64_t)value;
  return cpkt_lua_unsigned_make((unsigned int)(bits >> 32),
                                (unsigned int)bits);
}

__CPKT_LUA_SPECIAL_DEFINITIONS__

__CPKT_LUA_DEFINITIONS__
""".replace("__CPKT_LUA_SPECIAL_DEFINITIONS__", special_definitions()).replace(
        "__CPKT_LUA_DEFINITIONS__", definitions(items))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--include-dir", required=True, type=pathlib.Path)
    parser.add_argument("--header", required=True, type=pathlib.Path)
    parser.add_argument("--source", required=True, type=pathlib.Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    inputs = [args.include_dir / name for name in
              ("lua.h", "lauxlib.h", "lualib.h")]
    for path in inputs:
        if not path.is_file():
            raise ValueError("required input is missing: " + str(path))
    items = declarations(inputs)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.source.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(header(items), encoding="utf-8")
    args.source.write_text(source(items), encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print("generate_lua_c89_facade.py: " + str(error), file=sys.stderr)
        raise SystemExit(1)
