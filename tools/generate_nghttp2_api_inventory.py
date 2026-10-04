#!/usr/bin/env python3
"""Create a typed, target-specific nghttp2 public API inventory.

The script parses the pinned installed header with a host compiler only to
preserve declarations.  It never participates in compilation or packaging.
When a C89 facade header is supplied, every public dynamic function must have
an explicitly named cpkt_nghttp2_ counterpart.
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys
from typing import Any, Dict, Iterable, List, Set


NON_C89_TYPE_PATTERN = re.compile(
    r"\b(?:u?int(?:8|16|32|64)_t|ssize_t|nghttp2_ssize)\b|\blong\s+long\b")
FACADE_FUNCTION_PATTERN = re.compile(
    r"\b(cpkt_nghttp2_[A-Za-z0-9_]+)\s*\(")


def walk_ast(node: Any) -> Iterable[Dict[str, Any]]:
    if not isinstance(node, dict):
        return
    yield node
    for child in node.get("inner", []):
        yield from walk_ast(child)


def read_dynamic_functions(symbol_format: str, symbol_tool: str,
                           library: pathlib.Path) -> Set[str]:
    if symbol_format == "elf":
        command = [symbol_tool, "--dyn-syms", "--wide", str(library)]
    elif symbol_format == "macho":
        command = [symbol_tool, "-gU", str(library)]
    else:
        raise ValueError("unsupported symbol format: " + symbol_format)
    result = subprocess.run(command, check=True, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    names: Set[str] = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        if symbol_format == "elf":
            if (len(fields) >= 8 and fields[3] == "FUNC" and
                    fields[4] in {"GLOBAL", "WEAK"} and fields[6] != "UND"):
                names.add(fields[7].split("@", 1)[0])
        elif len(fields) >= 3 and fields[-2] in {"T", "t"}:
            names.add(fields[-1])
    return names


def build_umbrella(include_dir: pathlib.Path,
                   output_dir: pathlib.Path) -> pathlib.Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    source = output_dir / "nghttp2-api-inventory.c"
    source.write_text("#include <nghttp2/nghttp2.h>\n"
                      "int main(void) { return 0; }\n",
                      encoding="utf-8")
    return source


def function_declarations(ast: Dict[str, Any],
                          expected: Set[str]) -> Dict[str, Dict[str, Any]]:
    declarations: Dict[str, Dict[str, Any]] = {}
    for node in walk_ast(ast):
        name = node.get("name")
        if node.get("kind") != "FunctionDecl" or name not in expected:
            continue
        type_info = node.get("type", {})
        qual_type = type_info.get("qualType")
        if not qual_type:
            continue
        parameters = []
        for child in node.get("inner", []):
            if child.get("kind") != "ParmVarDecl":
                continue
            parameters.append({
                "name": child.get("name", ""),
                "type": child.get("type", {}).get("qualType", ""),
            })
        declarations[name] = {
            "type": qual_type,
            "parameters": parameters,
        }
    return declarations


def callback_typedefs(ast: Dict[str, Any]) -> Dict[str, str]:
    callbacks: Dict[str, str] = {}
    for node in walk_ast(ast):
        name = node.get("name", "")
        if node.get("kind") != "TypedefDecl" or not name.startswith("nghttp2_"):
            continue
        qual_type = node.get("type", {}).get("qualType", "")
        if "(*)" in qual_type:
            callbacks[name] = qual_type
    return callbacks


def record_declarations(ast: Dict[str, Any]) -> Dict[str, List[Dict[str, str]]]:
    records: Dict[str, List[Dict[str, str]]] = {}
    for node in walk_ast(ast):
        name = node.get("name", "")
        if node.get("kind") != "RecordDecl" or not name.startswith("nghttp2_"):
            continue
        fields = []
        for child in node.get("inner", []):
            if child.get("kind") != "FieldDecl":
                continue
            field_type = child.get("type", {}).get("qualType", "")
            if field_type:
                fields.append({"name": child.get("name", ""),
                               "type": field_type})
        if fields:
            records[name] = fields
    return records


def facade_functions(path: pathlib.Path) -> Set[str]:
    return set(FACADE_FUNCTION_PATTERN.findall(
        path.read_text(encoding="utf-8")))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--include-dir", required=True, type=pathlib.Path)
    parser.add_argument("--library", required=True, type=pathlib.Path)
    parser.add_argument("--symbol-format", required=True,
                        choices=("elf", "macho"))
    parser.add_argument("--symbol-tool", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--work-dir", required=True, type=pathlib.Path)
    parser.add_argument("--facade-header", type=pathlib.Path)
    parser.add_argument("--target")
    parser.add_argument("--sysroot", type=pathlib.Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    header = args.include_dir / "nghttp2" / "nghttp2.h"
    for path in (header, args.library):
        if not path.is_file():
            raise ValueError("required input is missing: " + str(path))

    dynamic = read_dynamic_functions(args.symbol_format, args.symbol_tool,
                                     args.library)
    public_dynamic = {name for name in dynamic if name.startswith("nghttp2_")}
    umbrella = build_umbrella(args.include_dir, args.work_dir)
    command = [args.clang, "-x", "c", "-std=c89", "-Wno-long-long",
               "-I", str(args.include_dir), "-Xclang", "-ast-dump=json",
               "-fsyntax-only", str(umbrella)]
    if args.target:
        command[1:1] = ["--target=" + args.target]
    if args.sysroot:
        command[1:1] = ["--sysroot=" + str(args.sysroot)]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise ValueError("nghttp2 declaration AST failed:\n" + result.stderr)
    ast = json.loads(result.stdout)
    declarations = function_declarations(ast, public_dynamic)
    missing_declarations = sorted(public_dynamic - set(declarations))
    if missing_declarations:
        raise ValueError("nghttp2 dynamic exports without a public declaration: " +
                         ", ".join(missing_declarations))

    typed_functions = sorted(
        name for name, declaration in declarations.items()
        if NON_C89_TYPE_PATTERN.search(declaration["type"]))
    required_facade_functions = {
        "cpkt_nghttp2_" + name[len("nghttp2_"):]
        for name in declarations
    }
    missing_facade_functions: List[str] = []
    if args.facade_header:
        if not args.facade_header.is_file():
            raise ValueError("facade header is missing: " +
                             str(args.facade_header))
        missing_facade_functions = sorted(
            required_facade_functions - facade_functions(args.facade_header))

    inventory = {
        "schema": 1,
        "dynamic_function_count": len(dynamic),
        "public_dynamic_function_count": len(public_dynamic),
        "declared_public_function_count": len(declarations),
        "functions": {name: declarations[name] for name in sorted(declarations)},
        "callback_typedefs": callback_typedefs(ast),
        "records": record_declarations(ast),
        "c89_typed_adapter_functions": typed_functions,
        "required_facade_functions": sorted(required_facade_functions),
        "missing_c89_facade_functions": missing_facade_functions,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(inventory, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    if missing_facade_functions:
        raise ValueError("nghttp2 C89 facade omits required functions: " +
                         ", ".join(missing_facade_functions))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("generate_nghttp2_api_inventory.py: " + str(error),
              file=sys.stderr)
        raise SystemExit(1)
