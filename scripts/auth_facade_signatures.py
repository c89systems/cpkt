#!/usr/bin/env python3
"""Reject public auth facade signature, callback and record drift on each SDK target.

The frozen declarations are intentionally independent of the native binding
names. Updating this file requires an interface change audit; there is no
automatic baseline update mode.
"""

import json
import pathlib
import re
import subprocess
import sys

sys.dont_write_bytecode = True

import auth_api_contract as native_contract
from generated_output_paths import validate_output_paths, write_generated_text

ROOT = pathlib.Path(__file__).resolve().parent.parent
SNAPSHOT = ROOT / "tests/contracts/auth_facade_signatures.json"
SOURCE = ("#include <cpkt/gssapi.h>\n"
          "#include <cpkt/sasl.h>\n"
          "#include <cpkt/sasl_plugin.h>\n")


def inspect(target, include_root=None):
    directory = native_contract.output_dir(target) / target
    source = directory / "facade.i"
    validate_output_paths(source)
    cc = native_contract.compiler(target)
    triple = native_contract.run([cc, "-dumpmachine"]).strip()
    include_root = include_root or ROOT / "include"
    prepared = native_contract.run(
        [cc, "-std=gnu99", "-E", "-x", "c", "-I", str(include_root), "-"],
        SOURCE)
    directory.mkdir(parents=True, exist_ok=True)
    write_generated_text(source, prepared)
    ast = json.loads(native_contract.run(
        ["clang", "-target", triple, "-x", "c", "-std=gnu99", "-Xclang",
         "-ast-dump=json", "-fsyntax-only", str(source)]))
    locate = native_contract.source_file_for_line(prepared)
    result = {"functions": {}, "records": {}, "record_order": {},
              "typedefs": {}, "enums": {}, "enum_order": [], "layouts": {}}
    for node in ast.get("inner", []):
        line = node.get("loc", {}).get("line")
        if line is None or "/include/cpkt/" not in locate(line):
            continue
        name = node.get("name", "")
        kind = node.get("kind")
        signature = node.get("type", {}).get("qualType", "")
        if kind == "FunctionDecl" and name.startswith("cpkt_"):
            result["functions"][name] = signature
        elif kind == "TypedefDecl" and name.startswith("cpkt_"):
            result["typedefs"][name] = signature
        elif kind == "RecordDecl" and name.startswith("cpkt_"):
            fields = {field["name"]: field.get("type", {}).get("qualType", "")
                      for field in node.get("inner", [])
                      if field.get("kind") == "FieldDecl" and field.get("name")}
            if fields:
                result["records"][name] = fields
                result["record_order"][name] = list(fields)
        elif kind == "EnumDecl":
            next_value = 0
            for item in node.get("inner", []):
                if item.get("kind") != "EnumConstantDecl":
                    continue
                value = next((child.get("value") for child in item.get("inner", [])
                              if child.get("kind") == "ConstantExpr"), None)
                if value is not None:
                    next_value = int(value, 0)
                if item.get("name", "").startswith("CPKT_"):
                    result["enums"][item["name"]] = next_value
                    result["enum_order"].append(item["name"])
                next_value += 1
    layout_dump = native_contract.run(
        ["clang", "-target", triple, "-x", "c", "-std=gnu99", "-Xclang",
         "-fdump-record-layouts-complete", "-fsyntax-only", str(source)])
    for section in layout_dump.split("*** Dumping AST Record Layout"):
        match = re.search(r"^\s*0 \| struct (cpkt_\w+)\s*$", section,
                          re.MULTILINE)
        if not match or match.group(1) not in result["records"]:
            continue
        name = match.group(1)
        fields = []
        for offset, field in re.findall(r"^\s*(\d+) \|   (.+)$", section,
                                        re.MULTILINE):
            field_name = re.search(r"([A-Za-z_]\w*)\s*$", field)
            if field_name and field_name.group(1) in result["records"][name]:
                fields.append([field_name.group(1), int(offset)])
        size = re.search(r"\[sizeof=(\d+), align=(\d+)\]", section)
        if size:
            result["layouts"][name] = {
                "fields": fields, "size": int(size.group(1)),
                "align": int(size.group(2))}
    if set(result["layouts"]) != set(result["records"]):
        raise RuntimeError("incomplete target record layouts on " + target)
    return result


def check(targets, include_root=None):
    validate_output_paths(*(native_contract.output_dir(target) / target / "facade.i"
        for target in targets))
    expected = json.loads(SNAPSHOT.read_text())
    for target in targets:
        observed = inspect(target, include_root)
        if observed != expected[target]:
            changes = []
            for category in observed:
                if observed[category] != expected[target][category]:
                    changes.append(category)
            raise RuntimeError("{} facade declaration drift: {}".format(
                target, ", ".join(changes)))
    print("auth facade signatures and callback records match {} target(s)".format(len(targets)))


if __name__ == "__main__":
    try:
        check(sys.argv[1:] or native_contract.TARGETS)
    except (OSError, ValueError, RuntimeError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
