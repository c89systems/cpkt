#!/usr/bin/env python3
"""Verify MQTT-C's public declarations have complete C89 facade coverage."""

import argparse
import json
import pathlib
import re
import subprocess
import sys
from typing import Set

sys.dont_write_bytecode = True
from generate_mqttc_c89_facade import functions, transform


def dynamic_symbols(tool: str, library: pathlib.Path) -> Set[str]:
    result = subprocess.run(
        [tool, "-D", "--defined-only", str(library)], check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    symbols: Set[str] = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 3 and re.match(r"(?:__)?mqtt_", fields[-1]):
            symbols.add(fields[-1].split("@", 1)[0])
    return symbols


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--include-dir", required=True, type=pathlib.Path)
    parser.add_argument("--library", required=True, type=pathlib.Path)
    parser.add_argument("--symbol-tool", required=True)
    parser.add_argument("--facade-header", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    native_header = args.include_dir / "mqtt.h"
    for path in (native_header, args.library, args.facade_header):
        if not path.is_file():
            raise ValueError("required input is missing: " + str(path))
    pal_header = args.include_dir / "mqtt_pal.h"
    if not pal_header.is_file():
        raise ValueError("required input is missing: " + str(pal_header))
    declared = {item[1] for item in
                functions(native_header.read_text(encoding="utf-8"))}
    declared.update(item[1] for item in
                    functions(pal_header.read_text(encoding="utf-8"), 2))
    dynamic = dynamic_symbols(args.symbol_tool, args.library)
    missing_dynamic = sorted(declared - dynamic)
    if missing_dynamic:
        raise ValueError("MQTT-C declarations missing dynamic definitions: " +
                         ", ".join(missing_dynamic))
    unheadered_dynamic = sorted(dynamic - declared)
    if unheadered_dynamic != ["mqtt_fixed_header_rules"]:
        raise ValueError("unexpected unheadered MQTT-C dynamic symbols: " +
                         ", ".join(unheadered_dynamic))
    facade_text = args.facade_header.read_text(encoding="utf-8")
    missing_facade = sorted(
        transform(name) for name in declared
        if not re.search(r"\b" + re.escape(transform(name)) + r"\s*\(",
                         facade_text))
    if missing_facade:
        raise ValueError("MQTT-C declarations missing C89 facade entries: " +
                         ", ".join(missing_facade))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({
        "schema": 1,
        "declared_function_count": len(declared),
        "declared_functions": sorted(declared),
        "dynamic_function_count": len(dynamic),
        "unheadered_dynamic_symbols": unheadered_dynamic,
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("generate_mqttc_api_inventory.py: " + str(error), file=sys.stderr)
        raise SystemExit(1)
