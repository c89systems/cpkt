#!/usr/bin/env python3
"""Inspect configured DB provider/public facade declarations without blessing drift.

The inventory command writes transient evidence below build/db-completion.
Stable native/facade snapshots and explicit binding decisions are separate,
reviewed contracts.
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
TARGETS = (
    "x86_64-linux-gnu", "x86_64-linux-musl", "aarch64-linux-gnu",
    "aarch64-linux-musl", "armhf-linux-gnu", "armhf-linux-musl",
    "arm64-apple-darwin",
)
HEADERS = {
    "postgres": ("libpq-fe.h", "libpq-events.h", "postgres_ext.h",
                 "libpq/libpq-fs.h"),
    "sqlite": ("sqlite3.h", "sqlite3session.h", "sqlite3rtree.h", "fts5.h",
               "sqlite3ext.h"),
}
DEPS = {"postgres": "postgresql", "sqlite": "sqlite"}
DEFINES = {
    "postgres": (),
    "sqlite": (
        "-DSQLITE_ENABLE_CARRAY=1", "-DSQLITE_ENABLE_NORMALIZE=1",
        "-DSQLITE_ENABLE_PREUPDATE_HOOK=1", "-DSQLITE_ENABLE_SESSION=1",
    ),
}
CLANG_TARGETS = {
    "x86_64-linux-gnu": "x86_64-linux-gnu",
    "x86_64-linux-musl": "x86_64-linux-musl",
    "aarch64-linux-gnu": "aarch64-linux-gnu",
    "aarch64-linux-musl": "aarch64-linux-musl",
    "armhf-linux-gnu": "armv7-linux-gnueabihf",
    "armhf-linux-musl": "armv7-linux-musleabihf",
    "arm64-apple-darwin": "arm64-apple-darwin25.4",
}


def run(command, input_text=None):
    result = subprocess.run(command, input=input_text, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            cwd=ROOT, check=False)
    if result.returncode:
        raise RuntimeError("{} failed:\n{}".format(
            " ".join(map(str, command)), result.stderr))
    return result.stdout


def compiler(target):
    return cache_value(ROOT, target, "CMAKE_C_COMPILER")


def output_dir(target):
    return scratch_dir(ROOT, target, "db-completion/inventory")


def origin_lookup(preprocessed):
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


def declarations(ast, origin, header_names, facade, preprocessed):
    result = {
        "functions": {}, "records": {}, "record_order": {},
        "typedefs": {}, "variables": {}, "enums": {}, "enum_order": [],
    }
    basenames = {pathlib.Path(name).name for name in header_names}
    newlines = [index for index, char in enumerate(preprocessed) if char == "\n"]

    def stable_type(value):
        # Clang spells anonymous public records with the preprocessing path.
        # Contracts are source controlled and must survive a new checkout.
        value = re.sub(r"/[^\s()]*?/install/include/", "", value)
        value = re.sub(r"/[^\s()]*?/postgresql/src/src/include/", "",
                       value)
        return value.replace(str(ROOT / "include") + "/", "")

    def visit(node, inherited_file="", inherited_line=None, parent=()):
        location = node.get("loc", {})
        line = location.get("line")
        if line is None and "offset" in location:
            line = bisect.bisect_right(newlines, location["offset"]) + 1
        if line is None:
            line = inherited_line
        file_name = origin(line) if line is not None else inherited_file
        if not file_name:
            file_name = inherited_file
        eligible = (pathlib.Path(file_name).name in basenames and
                    (("/include/cpkt/" in file_name) if facade else
                     ("/install/include/" in file_name or
                      "/postgresql/src/src/include/libpq/" in file_name)))
        kind = node.get("kind")
        name = node.get("name")
        signature = node.get("type", {}).get("qualType", "")
        signature = stable_type(signature)
        child_parent = parent
        if eligible and kind == "FunctionDecl" and name:
            result["functions"][name] = signature
        elif eligible and kind == "VarDecl" and name:
            result["variables"][name] = signature
        elif eligible and kind == "TypedefDecl" and name:
            result["typedefs"][name] = signature
        elif eligible and kind == "RecordDecl":
            fields = {
                field["name"]: stable_type(
                    field.get("type", {}).get("qualType", ""))
                for field in node.get("inner", [])
                if field.get("kind") == "FieldDecl" and field.get("name")
            }
            identity = name or "@" + str(location.get(
                "presumedLine", line if line is not None else "?"))
            child_parent = parent + (identity,)
            if fields:
                key = "{}:{}".format(pathlib.Path(file_name).name,
                                     ".".join(child_parent))
                result["records"][key] = fields
                result["record_order"][key] = list(fields)
        elif eligible and kind == "EnumDecl":
            next_value = 0
            for item in node.get("inner", []):
                if item.get("kind") != "EnumConstantDecl":
                    continue
                value = next(
                    (child.get("value") for child in item.get("inner", [])
                     if child.get("kind") == "ConstantExpr"), None)
                if value is not None:
                    next_value = int(value, 0)
                result["enums"][item["name"]] = next_value
                result["enum_order"].append(item["name"])
                next_value += 1
        for child in node.get("inner", []):
            visit(child, file_name, line, child_parent)

    for node in ast.get("inner", []):
        visit(node)
    return result


def record_layouts(dump, records):
    """Resolve clang's target-ABI offsets for every public record, including
    nested named records and anonymous unions (PGArgBlock)."""
    candidates = {}
    for block in dump.split("*** Dumping AST Record Layout"):
        title = re.search(r"^\s*\d+\s+\|\s+(struct|union)\s+(.+)$",
                          block, re.MULTILINE)
        size = re.search(r"\[sizeof=(\d+), align=(\d+)\]", block)
        if not title or not size:
            continue
        identity = title.group(2).strip()
        anonymous = re.search(r"(?:unnamed at|unnamed .*? at)\s+([^:]+):(\d+):",
                              identity)
        if anonymous:
            basename = pathlib.Path(anonymous.group(1)).name
            leaf = "@" + anonymous.group(2)
        else:
            basename = None
            leaf = identity.split("::", 1)[0].strip()
        offsets = {}
        for line in block.splitlines():
            field = re.match(r"^\s*(\d+)\s+\| {3}(?! )(.+?)\s+(\w+)$",
                             line)
            if field:
                offsets[field.group(3)] = int(field.group(1))
        candidates.setdefault((basename, leaf), []).append({
            "size": int(size.group(1)), "align": int(size.group(2)),
            "offsets": offsets,
        })
    result = {}
    for key, fields in records.items():
        basename, path = key.split(":", 1)
        leaf = path.split(".")[-1]
        possibilities = (candidates.get((basename, leaf), []) if leaf.startswith("@")
                         else candidates.get((None, leaf), []))
        matching = [item for item in possibilities
                    if all(field in item["offsets"] for field in fields)]
        if not matching:
            raise RuntimeError("target record layout missing: " + key)
        layout = matching[0]
        result[key] = {
            "size": layout["size"], "align": layout["align"],
            "offsets": {field: layout["offsets"][field] for field in fields},
        }
    return result


def inspect(target, provider, facade=False, include_override=None):
    include = (pathlib.Path(include_override) if include_override is not None
               else ROOT / "include" if facade else ROOT / ".cache" / "deps" /
               target / DEPS[provider] / "install" / "include")
    names = ((provider + ".h",) if facade else HEADERS[provider])
    if facade:
        source_names = ("cpkt/" + provider + ".h",)
    else:
        source_names = names
    source = "".join("#include <{}>\n".format(name) for name in source_names)
    directory = output_dir(target) / target
    stem = ("facade-" if facade else "native-") + provider
    path = directory / (stem + ".i")
    validate_output_paths(path, directory / (stem + ".json"))
    cc = compiler(target)
    flags = [] if facade else list(DEFINES[provider])
    if not facade and provider == "postgres":
        source_root = ROOT / ".cache" / "deps-build" / target / "postgresql" / "src" / "src" / "include"
        flags += ["-I", str(source_root)]
    prepared = run([cc, "-std=gnu99", "-E", "-x", "c", "-I", str(include),
                    *flags, "-"], source)
    directory.mkdir(parents=True, exist_ok=True)
    write_generated_text(path, prepared)
    clang_flags = ["-target", CLANG_TARGETS[target]]
    if target.startswith("armhf"):
        clang_flags += ["-mfloat-abi=hard"]
    ast = json.loads(run(
        ["clang", *clang_flags, "-x", "c", "-std=gnu99",
         "-Xclang", "-ast-dump=json", "-fsyntax-only", str(path)]))
    result = declarations(ast, origin_lookup(prepared), names, facade,
                          prepared)
    layout_dump = run(["clang", *clang_flags, "-x", "c", "-std=gnu99",
                       "-Xclang", "-fdump-record-layouts-complete",
                       "-fsyntax-only", str(path)])
    result["layouts"] = record_layouts(layout_dump, result["records"])
    if not facade:
        macros = run([cc, "-std=gnu99", "-dM", "-E", "-x", "c",
                      "-I", str(include), *flags, "-"], source)
        prefixes = (("PQ", "PG_COPYRES_", "PG_DIAG_", "LIBPQ_HAS_",
                     "INV_", "OID_MAX", "InvalidOid")
                    if provider == "postgres" else
                    ("SQLITE_", "FTS5_", "NOT_WITHIN", "PARTLY_WITHIN",
                     "FULLY_WITHIN"))
        result["macros"] = {
            match.group(1) + (match.group(2) or ""): match.group(3)
            for line in macros.splitlines()
            if (match := re.match(
                r"#define\s+(\w+)(\([^)]*\))?\s*(.*)", line))
            if match.group(1).startswith(prefixes)
        }
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", choices=TARGETS, required=True)
    parser.add_argument("--provider", choices=HEADERS, required=True)
    parser.add_argument("--facade", action="store_true")
    args = parser.parse_args()
    result = inspect(args.target, args.provider, args.facade)
    path = output_dir(args.target) / args.target / (
        ("facade-" if args.facade else "native-") + args.provider + ".json")
    write_generated_text(path, json.dumps(result, indent=2, sort_keys=True) + "\n")
    print("{} {} {}: {} functions, {} records, {} enums, {} macros".format(
        args.target, args.provider, "facade" if args.facade else "native",
        len(result["functions"]), len(result["records"]),
        len(result["enums"]), len(result.get("macros", {}))))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
