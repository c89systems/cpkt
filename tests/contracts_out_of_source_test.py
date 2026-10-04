#!/usr/bin/env python3
"""Run the affected contracts from a source root with no build directory."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
configured = Path(sys.argv[2]).resolve()
compiler = sys.argv[3]
with tempfile.TemporaryDirectory(prefix="contracts-extracted-", dir=configured) as name:
    root = Path(name)
    source = root / "source"
    binary = root / "binary"
    source.mkdir()
    binary.mkdir()
    shutil.copy2(configured / "CMakeCache.txt", binary / "CMakeCache.txt")
    shutil.copytree(repo / "scripts", source / "scripts")
    shutil.copytree(repo / "tests" / "contracts", source / "tests" / "contracts")
    for test in ('auth_contract_mutation_test.py',):
        shutil.copy2(repo / "tests" / test, source / "tests" / test)
    shutil.copytree(repo / "include" / "cpkt", source / "include" / "cpkt")
    shutil.copytree(repo / "cmake" / "exports", source / "cmake" / "exports")
    shutil.copy2(repo / "cmake" / "components.json", source / "cmake" / "components.json")
    (source / ".cache").symlink_to(repo / ".cache", target_is_directory=True)
    environment = os.environ.copy()
    environment["CPKT_CONFIGURED_BINARY_DIR"] = str(binary)
    commands = [['scripts/auth_api_contract.py', '--target', 'x86_64-linux-gnu'], ['scripts/auth_facade_bindings.py', '--target', 'x86_64-linux-gnu'], ['scripts/auth_facade_signatures.py', 'x86_64-linux-gnu'], ['scripts/auth_sasl_property_contract.py', 'x86_64-linux-gnu'], ['tests/auth_contract_mutation_test.py']]
    for command in commands:
        result = subprocess.run([sys.executable, str(source / command[0]),
                                 *command[1:]], cwd=source, env=environment,
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError("{} failed:\n{}{}".format(
                command[0], result.stdout, result.stderr))
        print(command[0] + ": " + result.stdout.strip().splitlines()[-1])
        if (source / "build").exists():
            raise RuntimeError("contract created a source-root build directory")
print("owned extracted-source contract paths passed without source-root build")
