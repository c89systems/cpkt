#!/usr/bin/env python3
"""Inventory the complete public libssh2 API from its three installed headers."""

import argparse
import json
import pathlib
import re
import subprocess
import sys
from typing import Any, Dict, Iterable, List, Set


def walk(node: Any) -> Iterable[Dict[str, Any]]:
    if not isinstance(node, dict):
        return
    yield node
    for child in node.get("inner", []):
        yield from walk(child)


def dynamic_symbols(tool: str, library: pathlib.Path) -> Set[str]:
    result = subprocess.run(
        [tool, "-D", "--defined-only", str(library)],
        check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    symbols: Set[str] = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[-1].startswith("libssh2_"):
            symbols.add(fields[-1].split("@", 1)[0])
    return symbols


def declarations(ast: Dict[str, Any]) -> Dict[str, Dict[str, Any]]:
    result: Dict[str, Dict[str, Any]] = {}
    for node in walk(ast):
        name = node.get("name", "")
        if node.get("kind") != "FunctionDecl" or not name.startswith("libssh2_"):
            continue
        type_name = node.get("type", {}).get("qualType", "")
        if not type_name:
            continue
        parameters: List[Dict[str, str]] = []
        for child in node.get("inner", []):
            if child.get("kind") == "ParmVarDecl":
                parameters.append({
                    "name": child.get("name", ""),
                    "type": child.get("type", {}).get("qualType", ""),
                })
        result[name] = {"type": type_name, "parameters": parameters}
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--include-dir", required=True, type=pathlib.Path)
    parser.add_argument("--library", required=True, type=pathlib.Path)
    parser.add_argument("--symbol-tool", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--work-dir", required=True, type=pathlib.Path)
    parser.add_argument("--facade-header", required=True, type=pathlib.Path)
    parser.add_argument("--target")
    parser.add_argument("--sysroot", type=pathlib.Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    include_dirs = [pathlib.Path(value) for value in
                    str(args.include_dir).split(";") if value]
    header_dir = next(
        (value for value in include_dirs if (value / "libssh2.h").is_file()),
        None)
    if header_dir is None:
        raise ValueError("libssh2.h is absent from include directories: " +
                         str(args.include_dir))
    headers = (
        header_dir / "libssh2.h",
        header_dir / "libssh2_sftp.h",
        header_dir / "libssh2_publickey.h",
    )
    for path in headers + (args.library, args.facade_header):
        if not path.is_file():
            raise ValueError("required input is missing: " + str(path))
    args.work_dir.mkdir(parents=True, exist_ok=True)
    umbrella = args.work_dir / "libssh2-api-inventory.c"
    umbrella.write_text(
        "#include <libssh2.h>\n"
        "#include <libssh2_sftp.h>\n"
        "#include <libssh2_publickey.h>\n"
        "int main(void) { return 0; }\n",
        encoding="utf-8")
    command = [
        args.clang, "-x", "c", "-std=c89", "-Wno-long-long",
        "-Xclang", "-ast-dump=json",
        "-fsyntax-only", str(umbrella),
    ]
    for include_dir in include_dirs:
        command[1:1] = ["-I", str(include_dir)]
    if args.target:
        command[1:1] = ["--target=" + args.target]
    if args.sysroot:
        command[1:1] = ["--sysroot=" + str(args.sysroot)]
    result = subprocess.run(
        command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise ValueError("libssh2 declaration AST failed:\n" + result.stderr)
    public = declarations(json.loads(result.stdout))
    dynamic = dynamic_symbols(args.symbol_tool, args.library)
    missing = sorted(dynamic - set(public))
    if missing:
        raise ValueError(
            "libssh2 dynamic exports without public declarations: " +
            ", ".join(missing))
    facade_text = args.facade_header.read_text(encoding="utf-8")
    missing_facade = sorted(
        "cpkt_" + name for name in dynamic
        if not re.search(r"\bcpkt_" + re.escape(name) + r"\s*\(",
                         facade_text))
    if missing_facade:
        raise ValueError(
            "libssh2 dynamic exports without C89 facade declarations: " +
            ", ".join(missing_facade))
    inventory = {
        "schema": 1,
        "headers": [path.name for path in headers],
        "dynamic_function_count": len(dynamic),
        "declared_public_function_count": len(public),
        "functions": {name: public[name] for name in sorted(public)},
        "dynamic_functions": sorted(dynamic),
        "facade_header": str(args.facade_header),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(inventory, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("generate_libssh2_api_inventory.py: " + str(error), file=sys.stderr)
        raise SystemExit(1)
