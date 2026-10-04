#!/usr/bin/env python3
"""Route inventory-declared early fixtures and CTest through identical proof."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
from cpkt_inventory import load, record
from cpkt_operation import operation_fds, delegated, run

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--group', required=True)
parser.add_argument('--target', required=True)
parser.add_argument('--test', required=True)
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('command', nargs=argparse.REMAINDER)
args = parser.parse_args()
root = args.root.resolve()
try:
    if 'CPKT_OPERATION_FD' not in os.environ:
        sys.exit(run(root, args.group, [sys.executable, __file__, *sys.argv[1:]]))
    fd, _ = delegated(root, args.group)
    item = record(load(root)['tests'], args.test)
    if not item.get('preflight'):
        raise RuntimeError('helper is not declared for proof dedup: ' + args.test)
    if args.group != 'all' and item['group'] not in (args.group, 'tooling'):
        raise RuntimeError('cross-group helper execution: ' + args.test)
    profile = args.target if item['target_sensitive'] else 'host'
    scratch = root / 'build/control/helper-work' / profile / args.test
    scratch.mkdir(parents=True, exist_ok=True)
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    # Only these fixture commands use the build directory as removable scratch.
    # Both early and later commands resolve to the same owned fixture workspace.
    command = [value.replace(str(args.binary), str(scratch)) for value in command]
    files = {root / path for path in item['command_inputs'] + item.get('helper_inputs', [])}
    pending = list(files)
    while pending:
        path = pending.pop()
        if path.suffix in ('.py', '.sh', '.cmake'):
            for name in re.findall(r'(?:cmake|scripts|tests)/[A-Za-z0-9_./-]+\.(?:py|sh|cmake|hpp|h|cpp|cxx|cc|c|json|patch|series|txt)(?![A-Za-z0-9_.])', path.read_text()):
                child = root / name
                if child.is_file() and child not in files:
                    files.add(child); pending.append(child)
    for value in command:
        path = Path(value)
        if path.is_file():
            files.add(path.resolve())
    proof = [sys.executable, str(root/'scripts/cpkt_helper_proof.py'), '--root', str(root),
             '--group', args.group, '--mode', 'preflight:' + profile, '--output', str(scratch)]
    for path in sorted(files):
        proof += ['--input', str(path)]
    for name in item['helper_environment']:
        proof += ['--environment', name]
    sys.exit(subprocess.call(proof + ['--'] + command, pass_fds=operation_fds()))
except (RuntimeError, OSError) as error:
    sys.exit(str(error))
