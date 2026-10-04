"""Execute CMake-registered preflight fixtures with inherited operation FDs."""
import json
import os
from pathlib import Path
import subprocess

from cpkt_operation import operation_fds


def run_registered_fixtures(binary, cases, ctest='ctest'):
    binary = Path(binary)
    listing = subprocess.check_output(
        [ctest, '--test-dir', str(binary), '--show-only=json-v1'],
        text=True, pass_fds=operation_fds())
    entries = json.loads(listing)['tests']
    found = {entry['name']: entry for entry in entries}
    if len(found) != len(entries) or set(found) != set(cases):
        raise RuntimeError('standalone preflight registration differs from required cases')
    for case in cases:
        entry = found[case]
        argv = entry.get('command', [])
        if not argv or not all(isinstance(value, str) and value for value in argv):
            raise RuntimeError('standalone preflight has no executable command: ' + case)
        properties = {item['name']: item['value'] for item in entry.get('properties', [])}
        if set(properties) - {'WORKING_DIRECTORY', 'ENVIRONMENT', 'TIMEOUT'}:
            raise RuntimeError('unsupported standalone preflight properties: ' + case)
        environment = dict(os.environ)
        for setting in properties.get('ENVIRONMENT', []):
            if '=' not in setting:
                raise RuntimeError('invalid standalone preflight environment: ' + case)
            key, value = setting.split('=', 1)
            environment[key] = value
        directory = properties.get('WORKING_DIRECTORY', str(binary))
        timeout = float(properties['TIMEOUT']) if 'TIMEOUT' in properties else None
        subprocess.run(argv, cwd=directory, env=environment, check=True,
                       timeout=timeout, pass_fds=operation_fds())
