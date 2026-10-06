#!/usr/bin/env python3
"""Freeze the configured providers' enabled public C declarations.

The checked-in snapshot is deliberately target specific.  Update it only
after inspecting the resulting diff and binding the new native API.
"""

import argparse
import bisect
import json
import pathlib
import re
import subprocess
import sys

from configured_build import cache_value, scratch_dir
from generated_output_paths import validate_output_paths, write_generated_text

ROOT = pathlib.Path(__file__).resolve().parent.parent
SNAPSHOT = ROOT / "tests" / "contracts" / "auth_native_api.json"
TARGETS = (
    "x86_64-linux-gnu",
    "x86_64-linux-musl",
    "aarch64-linux-gnu",
    "aarch64-linux-musl",
    "armhf-linux-gnu",
    "armhf-linux-musl",
    "arm64-apple-darwin",
)
SOURCES = {
    "gssapi": (
        "#include <gssapi/gssapi.h>\n"
        "#include <gssapi/gssapi_ext.h>\n"
        "#include <gssapi/gssapi_krb5.h>\n"
        "#include <gssapi/gssapi_generic.h>\n"
        "#include <gssapi/mechglue.h>\n"
        "#define inline __inline__\n"
        "#include <gssapi/gssapi_alloc.h>\n"
    ),
    "sasl": (
        "#include <sasl/sasl.h>\n"
        "#include <sasl/prop.h>\n"
        "#include <sasl/saslutil.h>\n"
        "#include <sasl/saslplug.h>\n"
    ),
}


def run(command, input_text=None):
    result = subprocess.run(command, input=input_text, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            cwd=ROOT, check=False)
    if result.returncode:
        raise RuntimeError("{} failed:\n{}".format(" ".join(map(str, command)),
                                                  result.stderr))
    return result.stdout


def compiler(target):
    return cache_value(ROOT, target, "CMAKE_C_COMPILER")


def output_dir(target):
    return scratch_dir(ROOT, target, "auth-completion/contract")


def source_file_for_line(preprocessed):
    lines = []
    files = []
    for number, line in enumerate(preprocessed.splitlines(), 1):
        match = re.match(r'^#\s+\d+\s+"([^"]+)"', line)
        if match:
            lines.append(number)
            files.append(match.group(1))

    def lookup(number):
        pos = bisect.bisect_right(lines, number) - 1
        return files[pos] if pos >= 0 else ""

    return lookup


def public_header(file_name, provider):
    return ("/include/gssapi/" if provider == "gssapi" else "/include/sasl/") in file_name


def records_and_declarations(ast, file_for_line, provider):
    result = {"functions": {}, "records": {}, "record_order": {},
              "typedefs": {}, "variables": {}, "enums": {},
              "enum_order": []}
    for node in ast.get("inner", []):
        location = node.get("loc", {})
        line = location.get("line")
        if line is None:
            continue
        file_name = file_for_line(line)
        if not public_header(file_name, provider):
            continue
        kind = node.get("kind")
        name = node.get("name")
        signature = node.get("type", {}).get("qualType", "")
        if kind == "FunctionDecl" and name and not name.startswith("__builtin"):
            previous = result["functions"].setdefault(name, signature)
            if previous != signature:
                raise RuntimeError("conflicting declaration: " + name)
        elif kind == "VarDecl" and name:
            result["variables"][name] = signature
        elif kind == "TypedefDecl" and name:
            result["typedefs"][name] = signature
        elif kind == "RecordDecl":
            fields = {field["name"]: field.get("type", {}).get("qualType", "")
                      for field in node.get("inner", [])
                      if field.get("kind") == "FieldDecl" and field.get("name")}
            if fields:
                key = "{}:{}".format(pathlib.Path(file_name).name,
                                     name or "@" + str(location.get("presumedLine", line)))
                result["records"][key] = fields
                result["record_order"][key] = list(fields)
        elif kind == "EnumDecl":
            next_value = 0
            for item in node.get("inner", []):
                if item.get("kind") != "EnumConstantDecl":
                    continue
                value = next((child.get("value") for child in item.get("inner", [])
                              if child.get("kind") == "ConstantExpr"), None)
                if value is not None:
                    next_value = int(value, 0)
                result["enums"][item["name"]] = next_value
                result["enum_order"].append(item["name"])
                next_value += 1
    return result


def macros(compiler_path, include, source, provider):
    data = run([compiler_path, "-std=gnu99", "-dM", "-E", "-x", "c",
                "-I", str(include), "-"], source)
    prefixes = ("GSS_", "gss_", "GSSAPI_", "KRB5_GSS_") if provider == "gssapi" else (
        "SASL_", "PROP_", "MD5_", "HMAC_", "UINT4")
    found = {}
    for line in data.splitlines():
        match = re.match(r"#define\s+(\w+)(\([^)]*\))?\s*(.*)", line)
        if match and match.group(1).startswith(prefixes):
            found[match.group(1) + (match.group(2) or "")] = match.group(3)
    return found


def inspect(target, provider, include_override=None):
    directory = output_dir(target) / target
    prepared = directory / (provider + ".i")
    validate_output_paths(prepared)
    name = "krb5" if provider == "gssapi" else "cyrus-sasl"
    include = include_override or ROOT / ".cache" / "deps" / target / name / "install" / "include"
    if not include.is_dir():
        raise RuntimeError("staged headers missing: " + str(include))
    cc = compiler(target)
    source = SOURCES[provider]
    preprocessed = run([cc, "-std=gnu99", "-E", "-x", "c", "-I",
                        str(include), "-"], source)
    directory.mkdir(parents=True, exist_ok=True)
    write_generated_text(prepared, preprocessed)
    triple = run([cc, "-dumpmachine"]).strip()
    ast = json.loads(run(["clang", "-target", triple, "-x", "c", "-std=gnu99", "-fblocks", "-Xclang",
                          "-ast-dump=json", "-fsyntax-only", str(prepared)]))
    result = records_and_declarations(ast, source_file_for_line(preprocessed),
                                      provider)
    result["macros"] = macros(cc, include, source, provider)
    return result


def check(targets, overrides=None):
    validate_output_paths(*(output_dir(target) / target / (provider + ".i")
        for target in targets for provider in SOURCES))
    expected = json.loads(SNAPSHOT.read_text())
    failures = []
    for target in targets:
        for provider in SOURCES:
            observed = inspect(target, provider,
                               None if overrides is None else overrides.get(provider))
            for category in ("functions", "records", "record_order", "typedefs", "variables", "enums", "enum_order", "macros"):
                if observed[category] != expected[target][provider][category]:
                    failures.append("{} {} {} changed".format(target, provider, category))
    if failures:
        raise RuntimeError("\n".join(failures))
    print("auth native API snapshot matches all {} configured targets".format(len(targets)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=TARGETS, action="append",
                        help="check only this configured target")
    args = parser.parse_args()
    check(args.target or TARGETS)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
