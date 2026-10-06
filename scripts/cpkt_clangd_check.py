#!/usr/bin/env python3
"""Hash the actual compiler header closure before same-run clangd reuse."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

from cpkt_lock import delegated


def dependency_inputs(entry):
    arguments = entry.get('arguments') or shlex.split(entry['command'])
    scan = []
    skip = False
    for argument in arguments:
        if skip:
            skip = False
            continue
        if argument in ('-o', '-MF', '-MT', '-MQ', '-MJ'):
            skip = True
        elif argument in ('-c', '-MD', '-MMD', '-MP', '-MG', '-M', '-MM'):
            continue
        elif any(argument.startswith(prefix) for prefix in ('-o', '-MF', '-MT', '-MQ', '-MJ')):
            continue
        else:
            scan.append(argument)
    result = subprocess.run(scan + ['-M', '-MT', 'cpkt-hover'], cwd=entry['directory'],
                            capture_output=True, text=True, check=True)
    dependencies = result.stdout.replace('\\\n', '')
    if not dependencies.startswith('cpkt-hover:'):
        raise RuntimeError('compiler did not emit a complete clangd dependency rule')
    paths = set()
    for token in re.findall(r'(?:\\.|[^\s])+', dependencies.split(':', 1)[1]):
        token = re.sub(r'\\(.)', r'\1', token).replace('$$', '$')
        path = Path(token)
        if not path.is_absolute():
            path = Path(entry['directory']) / path
        path = path.resolve()
        if not path.is_file():
            raise RuntimeError('clangd dependency input is missing: ' + str(path))
        paths.add(path)
    paths.add(Path(scan[0]).resolve())
    return sorted(paths)


def publish_editor_database(root,target):
    from cpkt_inventory import REPOSITORY_GROUP
    delegated(root,REPOSITORY_GROUP)
    native='arm64-apple-darwin' if sys.platform=='darwin' else 'x86_64-linux-gnu'
    if target!=native:raise ValueError('editor database must use the native target')
    from cpkt_receipts import canonical
    entries=[]
    for group in json.loads((root/'cmake/components.json').read_text())['groups']:
        build=root/'build'/target/group/'Debug'
        from cpkt_receipts import cache
        configured=cache(build/'CMakeCache.txt')
        if configured.get('CPKT_TARGET_ID')!=target or configured.get('CPKT_GROUP')!=group or configured.get('CMAKE_BUILD_TYPE')!='Debug':raise ValueError('editor database needs actual native Debug group graphs')
        database=json.loads((build/'compile_commands.json').read_text())
        if not database:raise ValueError('empty group editor database')
        for entry in database:
            arguments=entry.get('arguments') or shlex.split(entry['command'])
            if Path(arguments[0]).resolve()!=Path(configured['CMAKE_CXX_COMPILER'] if Path(entry['file']).suffix in ('.cpp','.cc','.cxx') else configured['CMAKE_C_COMPILER']).resolve():raise ValueError('editor compiler differs from configured compiler')
            if not Path(entry['file']).is_file():raise ValueError('editor source is missing')
        inventory=json.loads((root/'cmake/components.json').read_text())
        covered={str(Path(e['file']).resolve()) for e in database}
        for source in inventory['hover'][group]:
            if str((root/source).resolve()) not in covered:raise ValueError('editor database misses '+source)
        entries.extend(database)
    destination=root/'build/clangd/compile_commands.json'
    if any(p.is_symlink() for p in (destination,*destination.parents)):raise ValueError('editor database has a symlink ancestor')
    destination.parent.mkdir(parents=True,exist_ok=True)
    # Preserve command ordering: clangd uses the first matching variant.
    fd,name=tempfile.mkstemp(prefix='compile-commands-',suffix='.tmp',dir=destination.parent)
    temporary=Path(name)
    try:
        with os.fdopen(fd,'wb') as output:output.write(canonical(entries))
        temporary.replace(destination)
    finally:temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--publish-editor',action='store_true')
    parser.add_argument('--native-target')
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=False)
    parser.add_argument('--group', required=True)
    parser.add_argument('--source', type=Path, required=False)
    parser.add_argument('--checker', required=False)
    parser.add_argument('--gate', type=Path, required=False)
    args = parser.parse_args()
    delegated(args.root, args.group)
    if args.publish_editor:
        from cpkt_inventory import REPOSITORY_GROUP
        if args.group not in (REPOSITORY_GROUP,'all'):parser.error('editor publication requires repository scope')
        publish_editor_database(args.root,args.native_target);return
    if not all((args.build,args.source,args.checker,args.gate)):parser.error('selected check requires build/source/checker/gate')
    database = args.build / 'compile_commands.json'
    source = args.source.resolve()
    entries = [entry for entry in json.loads(database.read_text()) if Path(entry['file']).resolve() == source]
    if not entries:
        raise RuntimeError('compile database does not contain ' + str(source))
    # clangd selects the first command for a source compiled in multiple variants.
    inputs = dependency_inputs(entries[0])
    inputs += [source, database, args.gate, Path(__file__).resolve()]
    command = ['bash', str(args.root / 'scripts/helper.sh'),
               '--root', str(args.root), '--group', args.group, '--mode', 'clangd','--owned-build',str(args.build)]
    for name in ('CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'SDKROOT', 'LANG', 'LC_ALL'):
        command += ['--environment', name]
    for path in inputs:
        command += ['--input', str(path)]
    command += ['--', args.checker, '--check=' + str(source), '--compile-commands-dir=' + str(args.build)]
    os.execvp('bash', command)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit('clangd input closure: ' + str(error))
