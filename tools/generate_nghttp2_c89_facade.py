#!/usr/bin/env python3
"""Generate the complete strict-C89 public boundary for pinned nghttp2.

The pinned upstream header is the authority for the public API.  This generator
copies its public records, constants, callbacks, and all exported function
declarations into a C89 spelling, then emits one forwarding definition for
every public dynamic function.  Generated outputs are build artifacts; the
generator is the source-controlled implementation.
"""

import argparse
import pathlib
import re
import subprocess
import sys
from typing import Iterable, List, Tuple


FUNCTION_PATTERN = re.compile(
    r"^NGHTTP2_EXTERN\s+(?P<result>.*?)\s*"
    r"(?P<name>nghttp2_[A-Za-z0-9_]+)\s*"
    r"\((?P<parameters>.*?)\)\s*;",
    re.DOTALL | re.MULTILINE)
TYPE_REPLACEMENTS: Tuple[Tuple[str, str], ...] = (
    ("nghttp2_", "cpkt_nghttp2_"),
    ("nghttp2_ssize", "cpkt_nghttp2_ssize"),
    ("ssize_t", "cpkt_nghttp2_ssize"),
    ("uint64_t", "cpkt_nghttp2_u64"),
    ("uint32_t", "cpkt_nghttp2_u32"),
    ("int32_t", "cpkt_nghttp2_i32"),
    ("uint16_t", "cpkt_nghttp2_u16"),
    ("int16_t", "cpkt_nghttp2_i16"),
    ("uint8_t", "cpkt_nghttp2_u8"),
    ("int8_t", "cpkt_nghttp2_i8"),
)


def transform(text: str) -> str:
    """Translate an upstream declaration or record into its CPKT spelling."""
    text = text.replace("NGHTTP2_", "CPKT_NGHTTP2_")
    text = text.replace("CPKT_NGHTTP2_EXTERN", "CPKT_NGHTTP2_API")
    for native, facade in TYPE_REPLACEMENTS:
        if native.endswith("_"):
            text = text.replace(native, facade)
        else:
            text = re.sub(r"\b" + re.escape(native) + r"\b", facade, text)
    return text


def normalize(text: str) -> str:
    return " ".join(text.split())


def split_parameters(parameters: str) -> Iterable[str]:
    if normalize(parameters) == "void":
        return []
    depth = 0
    current: List[str] = []
    result: List[str] = []
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
    normalized = normalize(parameter)
    match = re.match(r"(?P<type>.+?)(?P<name>[A-Za-z_][A-Za-z0-9_]*)$",
                     normalized)
    if not match:
        raise ValueError("cannot identify parameter name: " + normalized)
    return match.group("type").rstrip(), match.group("name")


def function_definitions(header_text: str) -> str:
    definitions: List[str] = []
    matches = list(FUNCTION_PATTERN.finditer(header_text))
    if len(matches) != 181:
        raise ValueError("expected 181 nghttp2 public functions, found {}".format(
            len(matches)))
    for match in matches:
        native_result = normalize(match.group("result"))
        native_name = match.group("name")
        public_result = transform(match.group("result")).strip()
        public_name = transform(native_name)
        parameters = match.group("parameters")
        public_parameters = transform(parameters).strip()
        call_arguments: List[str] = []
        for parameter in split_parameters(parameters):
            native_type, name = parameter_type_and_name(parameter)
            if normalize(native_type) == "uint64_t":
                call_arguments.append("cpkt_nghttp2_native_u64(" + name + ")")
            else:
                call_arguments.append("(" + native_type + ")" + name)
        call = native_name + "(" + ", ".join(call_arguments) + ")"
        definition = "CPKT_NGHTTP2_API {} {}({}) {{\n".format(
            public_result, public_name, public_parameters)
        if native_result == "void":
            definition += "  {};\n".format(call)
        elif native_result == "nghttp2_vec":
            definition += (
                "  nghttp2_vec native_value;\n"
                "  cpkt_nghttp2_vec public_value;\n\n"
                "  native_value = {};\n"
                "  public_value.base = native_value.base;\n"
                "  public_value.len = native_value.len;\n"
                "  return public_value;\n".format(call))
        else:
            definition += "  return ({}){};\n".format(
                public_result, call)
        definition += "}\n"
        definitions.append(definition)
    return "\n".join(definitions)


def facade_header(header_text: str, version_text: str) -> str:
    start = header_text.find("typedef ptrdiff_t nghttp2_ssize;")
    end = header_text.rfind("#endif /* NGHTTP2_H */")
    if start < 0 or end < 0:
        raise ValueError("unable to locate nghttp2 public header body")
    version_match = re.search(
        r"#define NGHTTP2_VERSION \"(?P<version>[^\"]+)\"\s+"
        r".*?#define NGHTTP2_VERSION_NUM (?P<number>[^\s]+)",
        version_text, re.DOTALL)
    if not version_match:
        raise ValueError("unable to read nghttp2 version constants")
    native_body = header_text[start:end]
    linkage_end = re.search(
        r"\n#ifdef __cplusplus\s*\n}\s*\n#endif\s*\Z", native_body)
    if linkage_end is None:
        raise ValueError("unable to locate nghttp2 C++ linkage epilogue")
    body = transform(native_body[:linkage_end.start()])
    # C99 permits a comma after the final enum member; strict C89 does not.
    body = re.sub(r",(\s*}\s+cpkt_nghttp2_[A-Za-z0-9_]+;)", r"\1", body)
    return """/* Generated by tools/generate_nghttp2_c89_facade.py; do not edit. */
#ifndef CPKT_NGHTTP2_H
#define CPKT_NGHTTP2_H

#include <stdarg.h>
#include <stddef.h>

#if defined(_WIN32) && defined(CPKT_NGHTTP2_BUILDING_SHARED)
#define CPKT_NGHTTP2_API __declspec(dllexport)
#elif defined(_WIN32) && !defined(CPKT_NGHTTP2_STATIC)
#define CPKT_NGHTTP2_API __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#define CPKT_NGHTTP2_API __attribute__((visibility("default")))
#else
#define CPKT_NGHTTP2_API
#endif

#ifdef __cplusplus
extern "C" {{
#endif

typedef signed char cpkt_nghttp2_i8;
typedef unsigned char cpkt_nghttp2_u8;
typedef short cpkt_nghttp2_i16;
typedef unsigned short cpkt_nghttp2_u16;
typedef int cpkt_nghttp2_i32;
typedef unsigned int cpkt_nghttp2_u32;
typedef struct cpkt_nghttp2_u64 {{
  unsigned long high;
  unsigned long low;
}} cpkt_nghttp2_u64;

#define CPKT_NGHTTP2_VERSION "{version}"
#define CPKT_NGHTTP2_VERSION_NUM {number}

{body}

#ifdef __cplusplus
}}
#endif

#endif /* CPKT_NGHTTP2_H */
""".format(version=version_match.group("version"),
           number=version_match.group("number"), body=body)


def facade_source(header_text: str) -> str:
    return """/* Generated by tools/generate_nghttp2_c89_facade.py; do not edit. */
#include <limits.h>
#include <stdint.h>

#include <nghttp2/nghttp2.h>

#include <cpkt/nghttp2.h>

typedef char cpkt_nghttp2_u8_is_eight_bits[
    (sizeof(cpkt_nghttp2_u8) * CHAR_BIT == 8) ? 1 : -1];
typedef char cpkt_nghttp2_i32_is_32_bits[
    (sizeof(cpkt_nghttp2_i32) * CHAR_BIT == 32) ? 1 : -1];
typedef char cpkt_nghttp2_u32_is_32_bits[
    (sizeof(cpkt_nghttp2_u32) * CHAR_BIT == 32) ? 1 : -1];
typedef char cpkt_nghttp2_ssize_matches_native[
    (sizeof(cpkt_nghttp2_ssize) == sizeof(nghttp2_ssize)) ? 1 : -1];
typedef char cpkt_nghttp2_u64_word_is_at_least_32_bits[
    (sizeof(unsigned long) * CHAR_BIT >= 32) ? 1 : -1];
typedef char cpkt_nghttp2_u64_matches_native[
    (sizeof(uint64_t) * CHAR_BIT == 64) ? 1 : -1];

static uint64_t cpkt_nghttp2_native_u64(cpkt_nghttp2_u64 value) {{
  return ((uint64_t)value.high << 32) | (uint64_t)value.low;
}}

{definitions}
""".format(definitions=function_definitions(header_text))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--include-dir", required=True, type=pathlib.Path)
    parser.add_argument("--header", required=True, type=pathlib.Path)
    parser.add_argument("--source", required=True, type=pathlib.Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    native_header = args.include_dir / "nghttp2" / "nghttp2.h"
    version_header = args.include_dir / "nghttp2" / "nghttp2ver.h"
    for path in (native_header, version_header):
        if not path.is_file():
            raise ValueError("required input is missing: " + str(path))
    header_text = native_header.read_text(encoding="utf-8")
    version_text = version_header.read_text(encoding="utf-8")
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.source.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(facade_header(header_text, version_text),
                           encoding="utf-8")
    args.source.write_text(facade_source(header_text), encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("generate_nghttp2_c89_facade.py: " + str(error), file=sys.stderr)
        raise SystemExit(1)
