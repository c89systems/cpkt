#!/usr/bin/env python3
"""Exact same-run helper dedup; selected executable suites always execute."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import time
import shutil
from cpkt_operation import operation_fds, delegated
from cpkt_receipts import canonical, digest, file_identity, publish, read, tree_identity

def output_identity(path):
    return {p.relative_to(path).as_posix():file_identity(p) for p in sorted(path.rglob('*')) if p.is_file() or p.is_symlink()} if path.is_dir() else {}

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
parser.add_argument('--group', default=os.environ.get('GROUP', 'all'))
parser.add_argument('--input', type=Path, action='append', default=[])
parser.add_argument('--mode', required=True)
parser.add_argument('--owned-build',type=Path)
parser.add_argument('--output', type=Path, action='append', default=[])
parser.add_argument('--environment', action='append')
parser.add_argument('command', nargs=argparse.REMAINDER)
args = parser.parse_args()
command = args.command[1:] if args.command[:1] == ['--'] else args.command
if not command:
    parser.error('helper command required')
try:
    fd, owner = delegated(args.root, args.group)
    relevant = ({key: os.environ.get(key, '') for key in args.environment} if args.environment is not None else
        {key: value for key,value in os.environ.items() if key.startswith(('CPKT_', 'CMAKE_', 'CC', 'CXX', 'GROUP'))
         and not key.startswith('CPKT_OPERATION_')})
    executable = Path(shutil.which(command[0]) or command[0]).resolve()
    identity = digest(canonical({'command': command, 'mode': args.mode,
        'inputs': {str(path): file_identity(path) for path in args.input}, 'environment': relevant,
        'executable': file_identity(executable), 'runtime': {'python':sys.version,'platform':sys.platform,'os':list(os.uname())}, 'outputs': list(map(str,args.output))}))
    if args.owned_build:
        build=Path(os.path.abspath(args.owned_build))
        parts=build.relative_to(args.root.resolve()/'build').parts
        if args.mode!='clangd' or args.group=='all' or len(parts)!=3 or parts[1]!=args.group:raise RuntimeError('clangd proof must stay in its owned group graph')
        if any(p.is_symlink() for p in (build,*build.parents)):raise RuntimeError('clangd proof graph has a symlink ancestor')
        path=build/'clangd-proofs'/owner['run']/(identity+'.json')
    else:
        if args.mode=='clangd':raise RuntimeError('clangd proof requires its owned group graph')
        path = args.root / 'build/control/helper-proofs' / owner['run'] / (identity + '.json')
    try:
        old = read(path)
    except RuntimeError:
        old = None
    if old and old['run'] == owner['run'] and old['input_id'] == identity and old.get('outputs') == {str(p):output_identity(p) for p in args.output}:
        print('reused exact same-run helper: ' + ' '.join(command))
        with (args.root/'build/control/events.jsonl').open('a') as stream:
            stream.write(__import__('json').dumps({'run':owner['run'],'phase':'helper','status':'reused','input_id':identity,'command':command})+'\n')
        sys.exit(0)
    path.unlink(missing_ok=True)
    started = time.monotonic()
    status = subprocess.call(command, pass_fds=operation_fds())
    if status:
        sys.exit(status)
    publish(path, {'kind':'helper','group':args.group,'mode':args.mode,'input_id':identity,
                   'coverage':[command], 'outputs':{str(p):output_identity(p) for p in args.output}, 'seconds':time.monotonic()-started})
    with (args.root/'build/control/events.jsonl').open('a') as stream:
        stream.write(__import__('json').dumps({'run':owner['run'],'phase':'helper','status':'passed','input_id':identity,'command':command,'seconds':time.monotonic()-started})+'\n')
except (RuntimeError,OSError,ValueError) as error:
    sys.exit(str(error))
