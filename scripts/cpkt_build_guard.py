#!/usr/bin/env python3
"""Check inherited lock before a generated build command executes."""
import os
from pathlib import Path
import sys
from cpkt_operation import delegated
from cpkt_inventory import load
from cpkt_receipts import component_receipt, readiness_path, cache

try:
    root, group = Path(sys.argv[1]).resolve(), sys.argv[2]
    delegated(root, group)
    arguments = sys.argv[3:]
    # The generated command runs this launcher only for actual build work.
    # A no-op graph/lock query must not revoke completed verification.
    checking = any(Path(value).name == 'cpkt_operation.py' for value in arguments) and '--check' in arguments
    # CMake's package target only consumes verified outputs. Its custom rule
    # passes through this same launcher, but does not rebuild the graph.
    staging = (len(arguments) == 7
        and Path(arguments[1]).resolve() == root / 'scripts/cpkt_packages.py'
        and arguments[2] == ('stage-target' if group == 'all' else 'stage')
        and arguments[3:5] == ['--group', group]
        and arguments[5] == '--preset')
    if not checking and not staging:
        cwd = Path.cwd()
        component_base = root / '.cache/deps-build'
        if cwd.is_relative_to(component_base):
            parts = cwd.relative_to(component_base).parts
            if len(parts) >= 2:
                target, directory = parts[:2]
                for name, item in load(root)['components'].items():
                    if item['directory'] == directory:
                        if item['group'] != group:
                            raise RuntimeError('cross-group producer mutation: ' + name)
                        component_receipt(root, target, name).unlink(missing_ok=True)
                        for path in (root / 'build/verification' / target / group).glob('*-development.json'):
                            path.unlink(missing_ok=True)
        elif (cwd / 'CMakeCache.txt').is_file():
            configured = cache(cwd / 'CMakeCache.txt')
            if configured.get('CPKT_DEPENDENCY_PRODUCER') != 'ON' and 'CPKT_TARGET_ID' in configured:
                configuration = cwd.name if cwd.is_relative_to(root/'build'/configured['CPKT_TARGET_ID']/group) else configured['CMAKE_BUILD_TYPE']
                for suffix in ('development', 'built'):
                    path = root / 'build/verification' / configured['CPKT_TARGET_ID'] / group / (configuration + '-' + suffix + '.json')
                    path.unlink(missing_ok=True)
    os.execvp(sys.argv[3], sys.argv[3:])
except (RuntimeError, OSError) as error:
    sys.exit('Repository build requires scripts/cpkt_operation.py delegation: ' + str(error))
