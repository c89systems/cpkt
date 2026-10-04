#!/usr/bin/env python3
"""Create a typed, target-specific inventory of OpenSSL's public functions.

This is a development generator, not a build compiler.  It uses a compiler AST
only to preserve declarations from the pinned OpenSSL headers; the resulting
inventory is checked into no release artifact and must be consumed by the C89
facade generator before cpkt_openssl can ship.
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys
from typing import Any, Dict, Iterable, List, Set


COMPATIBILITY_EXPORTS = {
    "ERR_load_CRYPTO_strings",
    "OCSP_crlID_new",
    "OPENSSL_fork_child",
    "OPENSSL_fork_parent",
    "OPENSSL_fork_prepare",
}

# These names remain in OpenSSL's dynamic ABI/export manifest for historical
# consumers, but are not declared by the configured installed public headers.
# They are either OpenSSL-internal helpers or obsolete PEM CMS compatibility
# entry points.  They are not a public API contract for a new cpkt facade.
ABI_ONLY_EXPORTS = {
    "DSO_METHOD_openssl",
    "DSO_bind_func",
    "DSO_convert_filename",
    "DSO_ctrl",
    "DSO_dsobyaddr",
    "DSO_flags",
    "DSO_free",
    "DSO_get_filename",
    "DSO_global_lookup",
    "DSO_load",
    "DSO_merge",
    "DSO_new",
    "DSO_pathbyaddr",
    "DSO_set_filename",
    "DSO_up_ref",
    "OPENSSL_DIR_end",
    "OPENSSL_DIR_read",
    "PEM_read_CMS",
    "PEM_read_bio_CMS",
    "PEM_write_CMS",
    "PEM_write_bio_CMS",
    "asn1_d2i_read_bio",
    "conf_ssl_get",
    "conf_ssl_get_cmd",
    "conf_ssl_name_find",
    "err_free_strings_int",
}

# These spellings require a facade type rather than a directly consumable C89
# declaration.  Do not include size_t, ptrdiff_t, va_list, or time_t here:
# those are supplied by C89's standard headers.  Fixed-width 32-bit aliases
# are still C89 syntax through the upstream typedef; the target-independent
# representation problem is native 64-bit storage and long long syntax.
C89_TYPED_ADAPTER_PATTERN = re.compile(
    r"\b(?:int64_t|uint64_t|BN_ULONG|SHA_LONG64)\b|\blong\s+long\b")

# BIO_MMSG_CB_ARGS reaches public callbacks through a generic pointer rather
# than through a function declaration. Keep its facade helpers explicit so an
# AST signature scan cannot accidentally omit that usable-record boundary.
RECORD_FACADE_HELPERS = {
    "BIO_MMSG_CB_ARGS": {
        "cpkt_openssl_BIO_set_callback",
        "cpkt_openssl_BIO_get_callback",
        "cpkt_openssl_BIO_set_callback_ex",
        "cpkt_openssl_BIO_get_callback_ex",
    },
}

FACADE_FUNCTION_PATTERN = re.compile(
    r"\b(cpkt_openssl_[A-Za-z0-9_]+)\s*\(")


def read_num_files(paths: Iterable[pathlib.Path]) -> Set[str]:
    names: Set[str] = set()
    for path in paths:
        for line in path.read_text(encoding="utf-8").splitlines():
            fields = line.split()
            if len(fields) >= 4 and re.search(r"(^|,)EXIST::FUNCTION", fields[3]):
                names.add(fields[0])
    return names


def read_dynamic_functions(symbol_format: str, symbol_tool: str,
                           libraries: Iterable[pathlib.Path]) -> Set[str]:
    command: List[str]
    if symbol_format == "elf":
        command = [symbol_tool, "--dyn-syms", "--wide"]
    elif symbol_format == "macho":
        command = [symbol_tool, "-gU"]
    else:
        raise ValueError("unsupported symbol format: " + symbol_format)
    command.extend(str(path) for path in libraries)
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


def build_umbrella(include_dir: pathlib.Path, output_dir: pathlib.Path) -> pathlib.Path:
    headers = sorted((include_dir / "openssl").glob("*.h"))
    if not headers:
        raise ValueError("no OpenSSL headers found below " + str(include_dir))
    output_dir.mkdir(parents=True, exist_ok=True)
    source = output_dir / "openssl-api-inventory.c"
    source.write_text(
        "".join("#include <openssl/{}>\n".format(path.name) for path in headers)
        + "int main(void) { return 0; }\n",
        encoding="utf-8")
    return source


def walk_ast(node: Any) -> Iterable[Dict[str, Any]]:
    if not isinstance(node, dict):
        return
    yield node
    for child in node.get("inner", []):
        yield from walk_ast(child)


def function_declarations(ast: Dict[str, Any], expected: Set[str]) -> Dict[str, Dict[str, Any]]:
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
        candidate = {
            "type": qual_type,
            "parameters": parameters,
        }
        previous = declarations.get(name)
        if previous is None or len(candidate["parameters"]) > len(previous["parameters"]):
            declarations[name] = candidate
    return declarations


def requires_c89_typed_adapter(declaration: Dict[str, Any]) -> bool:
    spellings = [declaration["type"]]
    spellings.extend(parameter["type"] for parameter in declaration["parameters"])
    return any(C89_TYPED_ADAPTER_PATTERN.search(spelling) for spelling in spellings)


def record_declarations(ast: Dict[str, Any]) -> Dict[str, Dict[str, Any]]:
    declarations: Dict[str, Dict[str, Any]] = {}
    for node in walk_ast(ast):
        if node.get("kind") != "RecordDecl" or not node.get("name"):
            continue
        fields = []
        for child in node.get("inner", []):
            if child.get("kind") != "FieldDecl":
                continue
            field_type = child.get("type", {}).get("qualType")
            if not field_type:
                continue
            fields.append({"name": child.get("name", ""), "type": field_type})
        if not fields or not any(
                C89_TYPED_ADAPTER_PATTERN.search(field["type"])
                for field in fields):
            continue
        candidate = {"fields": fields}
        previous = declarations.get(node["name"])
        if previous is None or len(candidate["fields"]) > len(previous["fields"]):
            declarations[node["name"]] = candidate
    return declarations


def record_aliases(ast: Dict[str, Any], records: Dict[str, Dict[str, Any]]) -> Set[str]:
    aliases: Set[str] = set(records)
    for node in walk_ast(ast):
        if node.get("kind") != "TypedefDecl" or not node.get("name"):
            continue
        type_info = node.get("type", {})
        spellings = [type_info.get("qualType", ""),
                     type_info.get("desugaredQualType", "")]
        if any(record_name in spelling for record_name in records
               for spelling in spellings):
            aliases.add(node["name"])
    return aliases


def record_dependent_functions(declarations: Dict[str, Dict[str, Any]],
                               aliases: Set[str]) -> List[str]:
    pattern = re.compile(r"\b(?:" + "|".join(
        re.escape(alias) for alias in sorted(aliases)) + r")\b")
    return sorted(
        name for name, declaration in declarations.items()
        if any(pattern.search(spelling) for spelling in
               [declaration["type"]] + [
                   parameter["type"] for parameter in declaration["parameters"]]))


def facade_functions(path: pathlib.Path) -> Set[str]:
    return set(FACADE_FUNCTION_PATTERN.findall(
        path.read_text(encoding="utf-8")))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--include-dir", required=True, type=pathlib.Path)
    parser.add_argument("--num", required=True, action="append", type=pathlib.Path)
    parser.add_argument("--library", required=True, action="append", type=pathlib.Path)
    parser.add_argument("--symbol-format", required=True, choices=("elf", "macho"))
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
    for path in [args.include_dir, *args.num, *args.library]:
        if not path.exists():
            raise ValueError("required input is missing: " + str(path))

    nominal = read_num_files(args.num)
    dynamic = read_dynamic_functions(args.symbol_format, args.symbol_tool, args.library)
    unexpected = dynamic - nominal - COMPATIBILITY_EXPORTS
    if unexpected:
        raise ValueError("unclassified dynamic functions: " + ", ".join(sorted(unexpected)))
    exported_public_abi = nominal & dynamic

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
        raise ValueError("OpenSSL declaration AST failed:\n" + result.stderr)
    ast = json.loads(result.stdout)
    declarations = function_declarations(ast, exported_public_abi)
    typed_records = record_declarations(ast)
    typed_record_aliases = record_aliases(ast, typed_records)
    unresolved = set(exported_public_abi - set(declarations))
    unclassified_unresolved = sorted(unresolved - ABI_ONLY_EXPORTS)

    typed_functions = sorted(
        name for name, declaration in declarations.items()
        if requires_c89_typed_adapter(declaration))
    record_functions = record_dependent_functions(
        declarations, typed_record_aliases)
    required_facade_functions = {
        "cpkt_openssl_" + name
        for name in typed_functions + record_functions
    }
    for record_name, helpers in RECORD_FACADE_HELPERS.items():
        if record_name in typed_record_aliases:
            required_facade_functions.update(helpers)
    missing_facade_functions: List[str] = []
    if args.facade_header:
        if not args.facade_header.is_file():
            raise ValueError("facade header is missing: " +
                             str(args.facade_header))
        missing_facade_functions = sorted(
            required_facade_functions - facade_functions(args.facade_header))

    inventory = {
        "schema": 2,
        "nominal_function_count": len(nominal),
        "dynamic_function_count": len(dynamic),
        "exported_public_abi_function_count": len(exported_public_abi),
        "declared_public_function_count": len(declarations),
        "feature_disabled_functions": sorted(nominal - dynamic),
        "compatibility_exports": sorted(dynamic & COMPATIBILITY_EXPORTS),
        "functions": {name: declarations[name] for name in sorted(declarations)},
        "c89_typed_adapter_functions": typed_functions,
        "c89_typed_adapter_records": typed_records,
        "c89_typed_adapter_record_aliases": sorted(typed_record_aliases),
        "c89_record_dependent_functions": record_functions,
        "c89_required_facade_functions": sorted(required_facade_functions),
        "missing_c89_facade_functions": missing_facade_functions,
        "abi_only_exports": sorted(unresolved),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(inventory, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    if unclassified_unresolved:
        raise ValueError("unclassified ABI exports without a public header declaration: " +
                         ", ".join(unclassified_unresolved))
    if missing_facade_functions:
        raise ValueError("OpenSSL C89 facade omits required functions: " +
                         ", ".join(missing_facade_functions))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("generate_openssl_api_inventory.py: " + str(error), file=sys.stderr)
        raise SystemExit(1)
