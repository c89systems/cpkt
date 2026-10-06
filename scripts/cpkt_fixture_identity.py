#!/usr/bin/env python3
"""Describe an inventory-owned fixture's structural inputs; execute no commands."""
import argparse
import ast
import os
from pathlib import Path
import re
import sys
from cpkt_inventory import load, record
from cpkt_lock import delegated

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--group', required=True)
    parser.add_argument('--target', required=True)
    parser.add_argument('--test', required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    root = args.root.resolve()
    delegated(root, args.group)
    data = load(root)
    item = record(data['tests'], args.test)
    if not item.get('preflight') or item['group'] not in (data['repository_group'], 'tooling'):
        raise RuntimeError('fixture is not owned or not declared for preflight: '+args.test)
    profile = args.target if item.get('target_sensitive', False) else 'host'
    scratch = root/'build/control/helper-work'/profile/args.test
    if '..' in scratch.parts or not scratch.is_relative_to(root/'build') or any(p.is_symlink() for p in (scratch,*scratch.parents) if p!=root and p.is_relative_to(root)):
        raise RuntimeError('fixture scratch is not owned generated state')
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        raise RuntimeError('fixture has no executable command')
    command = [value.replace(str(args.binary), str(scratch)) for value in command]
    inputs = {root/path for path in item.get('command_inputs', [])+item.get('helper_inputs', [])}
    pending = list(inputs)
    while pending:
        path = pending.pop()
        if path.suffix not in ('.py', '.sh', '.cmake'):
            continue
        if path.suffix == '.py':
            for node in ast.walk(ast.parse(path.read_text())):
                modules=([node.module] if isinstance(node,ast.ImportFrom) and node.module else [item.name for item in node.names] if isinstance(node,ast.Import) else [])
                for module in modules:
                    for directory in (path.parent,root/'scripts',root/'tools'):
                        child=directory/(module.replace('.','/')+'.py')
                        if child.is_file() and child not in inputs:
                            inputs.add(child);pending.append(child)
        for name in re.findall(r'(?:cmake|scripts|tests)/[A-Za-z0-9_./-]+\.(?:py|sh|cmake|hpp|h|cpp|cxx|cc|c|json|patch|series|txt)(?![A-Za-z0-9_.])', path.read_text()):
            child = root/name
            if child.is_file() and child not in inputs:
                inputs.add(child)
                pending.append(child)
    for value in command:
        path = Path(value)
        if path.is_file():
            inputs.add(path.resolve())
    arguments = ['--root', str(root), '--group', data['repository_group'],
                 '--mode', 'preflight:'+profile, '--output', str(scratch)]
    for path in sorted(inputs):
        arguments += ['--input', str(path)]
    for name in item.get('helper_environment', []):
        arguments += ['--environment', name]
    sys.stdout.buffer.write(b'\0'.join(value.encode() for value in arguments+['--']+command)+b'\0')

if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        sys.exit('fixture identity: '+str(error))
