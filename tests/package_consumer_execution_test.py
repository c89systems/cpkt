#!/usr/bin/env python3
"""Prove native/QEMU consumer execution, argument handling and fail-fast behavior."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


if not __debug__:
    raise SystemExit("Package consumer tests require Python assertions enabled")
source = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="package-consumer-", dir=source / "build") as tmp:
    root = Path(tmp)
    calls = root / "calls.jsonl"
    env = os.environ.copy()
    env.update(CPKT_CONSUMER_CALLS=str(calls), SASL_PATH="untrusted-host-plugins")
    stub = """#!/usr/bin/env python3
import json, os, sys
with open(os.environ['CPKT_CONSUMER_CALLS'], 'a') as out:
    out.write(json.dumps([os.path.basename(sys.argv[0]), sys.argv[1:], os.environ.get('SASL_PATH')]) + '\\n')
sys.exit(7 if os.path.basename(sys.argv[0]) == 'cpkt_failure' else 0)
"""
    names = ["cpkt_cmake_sqlite_facade", "cpkt_cmake_iodbc_shared",
             "cpkt_cmake_lua_runtime_strict", "cpkt_cmake_sasl_facade",
             "cpkt_cmake_sasl_facade_shared", "cpkt_pkg_cpkt-lua_default"]
    paths = []
    for name in names + ["cpkt_failure", "cpkt_after_failure"]:
        path = root / name
        path.write_text(stub)
        path.chmod(0o755)
        if name in names:
            paths.append(str(path))
    runner = source / "scripts/run-package-consumers.sh"
    lua_file = str(root / "Lua fixture.lua")
    command = ["bash", str(runner), "--lua-file", lua_file]
    subprocess.run(command + ["--"] + paths, env=env, check=True, capture_output=True)
    records = [json.loads(line) for line in calls.read_text().splitlines()]
    assert [row[0] for row in records] == names, records
    assert records[2][1] == [lua_file], records
    assert records[3][2] == "/cpkt-no-external-sasl-plugins", records
    assert records[4][2] is None, records

    qemu = root / "QEMU fixture"
    qemu.write_text("""#!/usr/bin/env python3
import json, os, subprocess, sys
assert sys.argv[1:3] == ['-L', os.environ['CPKT_CONSUMER_SYSROOT']]
with open(os.environ['CPKT_CONSUMER_CALLS'], 'a') as out:
    out.write(json.dumps(['qemu', sys.argv[1:], None]) + '\\n')
sys.exit(subprocess.call(sys.argv[3:]))
""")
    qemu.chmod(0o755)
    env["CPKT_CONSUMER_SYSROOT"] = str(root / "sysroot with spaces")
    calls.write_text("")
    subprocess.run(command + ["--qemu", str(qemu), "--sysroot",
                             env["CPKT_CONSUMER_SYSROOT"], "--"] + paths,
                   env=env, check=True, capture_output=True)
    records = [json.loads(line) for line in calls.read_text().splitlines()]
    assert [row[0] for row in records[1::2]] == names, records
    assert all(row[0] == "qemu" for row in records[::2]), records

    calls.write_text("")
    result = subprocess.run(command + ["--", str(root / "cpkt_failure"),
                                       str(root / "cpkt_after_failure")],
                            env=env, capture_output=True, text=True)
    assert result.returncode == 7, result
    assert len(calls.read_text().splitlines()) == 1, calls.read_text()
    for args in (["--"], ["--", str(root / "missing")]):
        assert subprocess.run(command + args, env=env, capture_output=True).returncode != 0
    for args in (["--qemu", str(qemu)], ["--sysroot", env["CPKT_CONSUMER_SYSROOT"]]):
        assert subprocess.run(command + args + ["--"] + paths, env=env,
                              capture_output=True).returncode != 0
print("[test] package consumer execution passed")
