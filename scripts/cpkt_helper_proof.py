#!/usr/bin/env python3
"""Inspect/publish structured same-run helper evidence; Bash executes helpers."""
import argparse
import os
from pathlib import Path
import shutil
import sys
from cpkt_lock import delegated
from cpkt_receipts import canonical, digest, file_identity, publish, read

def output_identity(path):
    return {p.relative_to(path).as_posix(): file_identity(p) for p in sorted(path.rglob('*'))
            if p.is_file() or p.is_symlink()} if path.is_dir() else {}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--group', default=os.environ.get('GROUP', 'all'))
    parser.add_argument('--input', type=Path, action='append', default=[])
    parser.add_argument('--mode', required=True)
    parser.add_argument('--owned-build', type=Path)
    parser.add_argument('--output', type=Path, action='append', default=[])
    parser.add_argument('--environment', action='append', default=[])
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--publish', action='store_true')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command or args.check == args.publish:
        parser.error('one of --check/--publish and a command identity are required')
    _, owner = delegated(args.root, args.group)
    relevant = {key: os.environ.get(key, '') for key in args.environment}
    executable = Path(shutil.which(command[0]) or command[0]).resolve()
    identity = digest(canonical({'command': command, 'mode': args.mode,
        'inputs': {str(path): file_identity(path) for path in args.input},
        'environment': relevant, 'executable': file_identity(executable),
        'runtime': {'python': sys.version, 'platform': sys.platform, 'os': list(os.uname())},
        'outputs': list(map(str, args.output))}))
    if args.owned_build:
        build = Path(os.path.abspath(args.owned_build))
        parts = build.relative_to(args.root.resolve()/'build').parts
        if args.mode != 'clangd' or args.group == 'all' or len(parts) != 3 or parts[1] != args.group:
            raise RuntimeError('clangd proof must stay in its owned graph')
        if any(p.is_symlink() for p in (build, *build.parents)):
            raise RuntimeError('clangd proof graph has a symlink ancestor')
        path = build/'clangd-proofs'/owner['run']/(identity+'.json')
    else:
        if args.mode == 'clangd':
            raise RuntimeError('clangd proof requires its owned graph')
        path = args.root/'build/control/helper-proofs'/owner['run']/(identity+'.json')
    if args.check:
        try:
            previous = read(path)
        except RuntimeError:
            return 10
        if (previous['run'] == owner['run'] and previous['input_id'] == identity
                and previous.get('outputs') == {str(p): output_identity(p) for p in args.output}):
            print('reused exact same-run helper: '+' '.join(command))
            return 0
        path.unlink(missing_ok=True)
        return 10
    publish(path, {'kind': 'helper', 'group': args.group, 'mode': args.mode,
                   'input_id': identity, 'coverage': [command],
                   'outputs': {str(p): output_identity(p) for p in args.output}})
    return 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        sys.exit('helper evidence: '+str(error))
