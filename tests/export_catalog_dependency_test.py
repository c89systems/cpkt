#!/usr/bin/env python3
"""Exercise the repository export helper through real incremental shared links."""

import argparse
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path


SYMBOLS = ("cpkt_export_probe_alpha", "cpkt_export_probe_beta")


def cache_value(cache: Path, key: str) -> str:
    match = re.search(rf"^{re.escape(key)}:[^=]*=(.*)$", cache.read_text(), re.M)
    if match is None or not match.group(1):
        raise AssertionError(f"missing {key} in {cache}")
    return match.group(1)


def run(*command: str) -> str:
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    if result.returncode:
        raise AssertionError(f"{' '.join(command)} exited {result.returncode}:\n{result.stdout}")
    return result.stdout


def helper_source(root: Path) -> str:
    source = (root / "CMakeLists.txt").read_text()
    match = re.search(
        r"^function\(cpkt_apply_auth_export_catalog target_name catalog_name\)\n"
        r".*?^endfunction\(\)", source, re.M | re.S)
    if match is None:
        raise AssertionError("actual cpkt_apply_auth_export_catalog function not found")
    return match.group(0)


def exports(nm: str, library: Path, darwin: bool) -> set[str]:
    command = (nm, "-gU", str(library)) if darwin else (
        nm, "-D", "--defined-only", str(library))
    names = set()
    for line in run(*command).splitlines():
        name = line.split()[-1] if line.split() else ""
        if name.startswith("_cpkt_export_probe_") and darwin:
            names.add(name[1:])
        elif name.startswith("cpkt_export_probe_") and not darwin:
            names.add(name)
    return names


def check_state(nm: str, library: Path, linker_file: Path,
                expected: set[str], darwin: bool) -> None:
    contents = linker_file.read_text()
    actual = exports(nm, library, darwin)
    listed = {name for name in SYMBOLS if
              (f"_{name}\n" if darwin else f"{name};") in contents}
    if listed != expected or actual != expected:
        raise AssertionError(
            f"catalog-only build stale: expected={sorted(expected)} "
            f"linker={sorted(listed)} dynamic={sorted(actual)}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--configured-binary-dir", type=Path, required=True)
    parser.add_argument("--generator", choices=("Ninja", "Unix Makefiles"),
                        required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    cache = args.configured_binary_dir.resolve() / "CMakeCache.txt"
    compiler = cache_value(cache, "CMAKE_C_COMPILER")
    toolchain = cache_value(cache, "CMAKE_TOOLCHAIN_FILE")
    target = cache_value(cache, "CPKT_TARGET_ID")
    nm = cache_value(cache, "CMAKE_NM")
    darwin = target == "arm64-apple-darwin"
    if target not in (
        "x86_64-linux-gnu", "x86_64-linux-musl", "aarch64-linux-gnu",
        "aarch64-linux-musl", "armhf-linux-gnu", "armhf-linux-musl",
        "arm64-apple-darwin"):
        raise AssertionError(f"unsupported configured target: {target}")
    scratch_parent = root / "build" / "export-catalog-contract"
    scratch_parent.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix=f"{target}-", dir=scratch_parent))
    try:
        fixture = scratch / "source"
        build = scratch / "build"
        catalog = fixture / "cmake" / "exports" / "export_probe.txt"
        catalog.parent.mkdir(parents=True)
        shutil.copy2(root / "cmake/CpktMutationPaths.cmake",
                     fixture / "cmake/CpktMutationPaths.cmake")
        catalog.write_text(SYMBOLS[0] + "\n")
        (fixture / "probe.c").write_text(
            "int cpkt_export_probe_alpha(void) { return 1; }\n"
            "int cpkt_export_probe_beta(void) { return 2; }\n")
        (fixture / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.25)\n"
            "project(cpkt_export_probe C)\n" + helper_source(root) + "\n"
            "add_library(export_probe SHARED probe.c)\n"
            "target_compile_options(export_probe PRIVATE -std=c89 -Wall -Wextra -Werror)\n"
            "cpkt_apply_auth_export_catalog(export_probe export_probe)\n")
        run("cmake", "-S", str(fixture), "-B", str(build), "-G",
            args.generator, f"-DCMAKE_TOOLCHAIN_FILE={toolchain}")
        fixture_cache = build / "CMakeCache.txt"
        if cache_value(fixture_cache, "CMAKE_C_COMPILER") != compiler:
            raise AssertionError("fixture selected a different compiler")
        if cache_value(fixture_cache, "CMAKE_NM") != nm:
            raise AssertionError("fixture selected a different target nm")
        library = build / ("libexport_probe.dylib" if darwin else
                           "libexport_probe.so")
        linker_file = build / ("export_probe.exports" if darwin else
                               "export_probe.map")
        run("cmake", "--build", str(build), "--target", "export_probe")
        check_state(nm, library, linker_file, {SYMBOLS[0]}, darwin)
        for expected in (
            {SYMBOLS[0], SYMBOLS[1]}, {SYMBOLS[1]},
            {SYMBOLS[0], SYMBOLS[1]}, {SYMBOLS[0]}
        ):
            time.sleep(1.1)
            catalog.write_text("".join(name + "\n" for name in SYMBOLS
                                       if name in expected))
            before = library.stat().st_mtime_ns
            run("cmake", "--build", str(build), "--target", "export_probe")
            check_state(nm, library, linker_file, expected, darwin)
            if library.stat().st_mtime_ns == before:
                raise AssertionError("catalog edit did not relink the shared library")
            idle_library = library.stat().st_mtime_ns
            idle_linker = linker_file.stat().st_mtime_ns
            run("cmake", "--build", str(build), "--target", "export_probe")
            if (library.stat().st_mtime_ns != idle_library or
                    linker_file.stat().st_mtime_ns != idle_linker):
                raise AssertionError("unchanged build rewrote or relinked exports")
        print(f"{target} {args.generator}: four catalog-only edits and idle "
              "rebuilds preserved exact dynamic exports")
    finally:
        shutil.rmtree(scratch)


if __name__ == "__main__":
    main()
