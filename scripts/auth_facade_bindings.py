#!/usr/bin/env python3
"""Check native-to-facade bindings as typed C89 consumer expressions."""

import argparse
import json
import pathlib
import re
import subprocess
import sys

from configured_build import cache_value, scratch_dir

ROOT = pathlib.Path(__file__).resolve().parent.parent
TARGETS = (
    "x86_64-linux-gnu", "x86_64-linux-musl", "aarch64-linux-gnu",
    "aarch64-linux-musl", "armhf-linux-gnu", "armhf-linux-musl",
    "arm64-apple-darwin",
)


def compiler(target):
    return cache_value(ROOT, target, "CMAKE_C_COMPILER")


def symbol_tool(target):
    return cache_value(ROOT, target, "CMAKE_NM")


def defined_symbols(target, static):
    suffix = ".a" if static else (".dylib" if target == "arm64-apple-darwin" else ".so")
    library = ROOT / ".cache/deps" / target / "krb5/install/lib" / ("libgssapi_krb5" + suffix)
    command = ([symbol_tool(target), "-gU", str(library)]
               if target == "arm64-apple-darwin" else
               [symbol_tool(target), "-g" if static else "-D",
                "--defined-only", str(library)])
    result = subprocess.run(command, cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError("native symbol audit failed: " + result.stderr)
    symbols = set()
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) < 2 or parts[-2] not in {"T", "D", "R", "B", "W", "V", "S"}:
            continue
        name = parts[-1].split("@", 1)[0]
        symbols.add(name[1:] if target == "arm64-apple-darwin" and name.startswith("_") else name)
    return symbols


def check(selected_targets):
    snapshot = json.loads((ROOT / "tests/contracts/auth_native_api.json").read_text())
    bindings = json.loads((ROOT / "tests/contracts/auth_bindings.json").read_text())
    record_bindings = json.loads((ROOT / "tests/contracts/auth_record_bindings.json").read_text())
    variable_bindings = json.loads((ROOT / "tests/contracts/auth_variable_bindings.json").read_text())
    exports = {provider: set((ROOT / "cmake/exports" /
                              ("cpkt_" + provider + ".txt")).read_text().split())
               for provider in bindings}
    for provider in bindings:
        native = set().union(*(set(snapshot[target][provider]["functions"])
                               for target in TARGETS))
        if set(bindings[provider]) != native:
            raise RuntimeError("unmapped native {} declarations: missing={} stale={}".format(
                provider, sorted(native - set(bindings[provider])),
                sorted(set(bindings[provider]) - native)))
        for symbol, binding in bindings[provider].items():
            if ("facade" in binding) == ("unavailable" in binding):
                raise RuntimeError("invalid binding: " + symbol)
            for entry in binding.get("facade", []):
                if "." not in entry and entry not in exports[provider]:
                    raise RuntimeError("binding lacks exported symbol: " + entry)
        records = snapshot[TARGETS[0]][provider]["records"]
        if set(record_bindings[provider]) != set(records):
            raise RuntimeError("unmapped native {} records".format(provider))
        for record, native_fields in records.items():
            mapped_fields = record_bindings[provider][record]
            if set(mapped_fields) != set(native_fields):
                raise RuntimeError("unmapped native {} fields: {}".format(
                    record, sorted(set(native_fields) - set(mapped_fields))))
            for field, entry in mapped_fields.items():
                if isinstance(entry, dict):
                    if not field.startswith("spare_") or "reserved" not in entry:
                        raise RuntimeError("invalid reserved field: " + record + "." + field)
                elif "." not in entry and entry not in exports[provider]:
                    raise RuntimeError("record field lacks exported accessor: " + entry)
    variables = set().union(*(set(snapshot[target]["gssapi"]["variables"])
                              for target in TARGETS))
    if set(variable_bindings["gssapi"]) != variables:
        raise RuntimeError("unmapped GSS OID variables: {}".format(
            sorted(variables - set(variable_bindings["gssapi"]))))
    for symbol, binding in variable_bindings["gssapi"].items():
        if ("facade" in binding) == ("unavailable" in binding):
            raise RuntimeError("invalid GSS OID variable binding: " + symbol)
        if "facade" in binding and binding["facade"] not in exports["gssapi"]:
            raise RuntimeError("GSS OID variable accessor is not exported: " + symbol)
    static_only = {"gssspi_exchange_meta_data", "gssspi_query_mechanism_info",
                   "gssspi_query_meta_data", "krb5_gss_oid_array",
                   "GSS_C_INQ_ODBC_SESSION_KEY"}
    absent = {"gss_export_name_object", "gss_import_name_object",
              "gss_initialize"}
    for target in selected_targets:
        shared_symbols = defined_symbols(target, False)
        static_symbols = defined_symbols(target, True)
        for symbol in static_only:
            if symbol in shared_symbols or symbol not in static_symbols:
                raise RuntimeError("expected static-only native symbol changed on {}: {}".format(
                    target, symbol))
        for symbol in absent:
            if symbol in shared_symbols or symbol in static_symbols:
                raise RuntimeError("expected unavailable native symbol changed on {}: {}".format(
                    target, symbol))
    expressions = sorted(set(entry for provider in bindings.values()
                             for binding in provider.values()
                             for entry in binding.get("facade", [])) |
                         set(entry for provider in record_bindings.values()
                             for record in provider.values()
                             for entry in record.values() if isinstance(entry, str)) |
                         set(binding["facade"] for binding in variable_bindings["gssapi"].values()
                             if "facade" in binding))
    lines = ["#include <cpkt/gssapi.h>", "#include <cpkt/sasl.h>",
             "#include <cpkt/sasl_plugin.h>", "void cpkt_auth_probe(void) {"]
    for entry in expressions:
        if "." in entry:
            record, field = entry.split(".", 1)
            lines.append("  (void)sizeof((({0} *)0)->{1});".format(record, field))
        else:
            lines.append("  (void)&{};".format(entry))
    lines.append("}")
    for target in selected_targets:
        directory = scratch_dir(ROOT, target, "auth-completion/contract")
        directory.mkdir(parents=True, exist_ok=True)
        source = directory / "typed-bindings.c"
        source.write_text("\n".join(lines) + "\n")
        command = [compiler(target), "-std=c89", "-pedantic-errors", "-Werror",
                   "-I", str(ROOT / "include"), "-c", str(source), "-o",
                   str(directory / ("typed-bindings-" + target + ".o"))]
        result = subprocess.run(command, cwd=ROOT, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode:
            raise RuntimeError("typed facade bindings fail on {}:\n{}".format(
                target, result.stderr))
    constants = json.loads((ROOT / "tests/contracts/auth_constant_bindings.json").read_text())
    for provider in bindings:
        native = set().union(*(set(snapshot[target][provider]["macros"])
                               for target in TARGETS))
        if set(constants[provider]) != native:
            raise RuntimeError("unmapped native {} macros: {}".format(
                provider, sorted(native - set(constants[provider]))))
    native_enums = set().union(*(set(snapshot[target]["sasl"]["enums"])
                                 for target in TARGETS))
    if set(constants["sasl_enums"]) != native_enums:
        raise RuntimeError("unmapped native SASL enum values: {}".format(
            sorted(native_enums - set(constants["sasl_enums"]))))
    for target in selected_targets:
        for provider in bindings:
            dependency = "krb5" if provider == "gssapi" else "cyrus-sasl"
            native_include = ROOT / ".cache/deps" / target / dependency / "install/include"
            includes = ["#include <cpkt/{}.h>".format(provider)]
            if provider == "gssapi":
                includes += ["#include <gssapi/gssapi.h>",
                             "#include <gssapi/gssapi_ext.h>",
                             "#include <gssapi/gssapi_krb5.h>"]
            else:
                includes += ["#include <cpkt/sasl_plugin.h>",
                             "#include <sasl/sasl.h>",
                             "#include <sasl/saslplug.h>"]
            checks = []
            for name, binding in constants[provider].items():
                kind = binding.get("kind")
                if kind not in ("numeric", "string"):
                    continue
                expression = name
                facade = binding["facade"]
                if kind == "numeric" and "(" in name:
                    expression = name[:name.index("(")] + "(0x10203040UL)"
                    facade += "(0x10203040UL)"
                if kind == "string":
                    checks.append("typedef char cpkt_auth_constant_{}[__builtin_strcmp({}, {}) == 0 ? 1 : -1];".format(
                        len(checks), expression, facade))
                else:
                    checks.append("typedef char cpkt_auth_constant_{}[({}) == ({}) ? 1 : -1];".format(
                        len(checks), expression, facade))
            if provider == "sasl":
                for name, facade in constants["sasl_enums"].items():
                    checks.append("typedef char cpkt_auth_constant_{}[((int)({})) == ((int)({})) ? 1 : -1];".format(
                        len(checks), name, facade))
            probe = directory / ("constant-bindings-" + provider + ".c")
            probe.write_text("\n".join(includes + checks) + "\n")
            command = [compiler(target), "-std=gnu99", "-Werror", "-I",
                       str(ROOT / "include"), "-I", str(native_include),
                       "-c", str(probe), "-o",
                       str(directory / ("constants-" + provider + "-" + target + ".o"))]
            result = subprocess.run(command, cwd=ROOT, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if result.returncode:
                raise RuntimeError("constant parity fails on {} {}:\n{}".format(
                    target, provider, result.stderr))
    print("{} configured targets: {} typed native-to-facade mappings and constant parity".format(
        len(selected_targets), len(expressions)))


if __name__ == "__main__":
    try:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("--target", choices=TARGETS, action="append")
        arguments = parser.parse_args()
        check(arguments.target or TARGETS)
    except (OSError, ValueError, RuntimeError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
