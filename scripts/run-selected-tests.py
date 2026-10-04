#!/usr/bin/env python3
"""Check selected CTest executables before running any test in the group."""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test-dir', required=True, type=Path)
    parser.add_argument('--regex', required=True)
    parser.add_argument('--verbose', action='store_true')
    args = parser.parse_args()
    command = ['ctest', '--test-dir', str(args.test_dir),
               '--no-tests=error', '-R', args.regex]
    inventory = subprocess.run(command + ['--show-only=json-v1'],
                               text=True, capture_output=True)
    if inventory.returncode:
        sys.stderr.write(inventory.stdout + inventory.stderr)
        return inventory.returncode
    tests = json.loads(inventory.stdout)['tests']
    if not tests:
        raise ValueError('no tests selected by ' + args.regex)
    missing = []
    for test in tests:
        argv = test.get('command', [])
        if not argv or not Path(argv[0]).is_file() or not os.access(argv[0], os.X_OK):
            missing.append(test['name'])
    if missing:
        raise ValueError('selected tests have missing or non-executable commands: '
                         + ', '.join(missing)
                         + '; build their test targets before running this group')
    print('Verified executables for %d selected tests' % len(tests), flush=True)
    return subprocess.run(command + [
        '--verbose' if args.verbose else '--output-on-failure']).returncode


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError) as error:
        print('run-selected-tests: ' + str(error), file=sys.stderr)
        sys.exit(2)
