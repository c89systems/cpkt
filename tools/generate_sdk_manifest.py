#!/usr/bin/env python3
"""CMake install generator: encode portable SDK payload and ABI metadata."""
import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(',', ':'), allow_nan=False).encode('utf-8')


def sha(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1048576), b''):
            value.update(chunk)
    return value.hexdigest()


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prefix', type=Path, required=True)
parser.add_argument('--components', type=Path, required=True)
parser.add_argument('--inventory', type=Path, required=True)
parser.add_argument('--version', required=True)
parser.add_argument('--target', required=True)
parser.add_argument('--tool', required=True)
parser.add_argument('--floor', default='')
args = parser.parse_args()
prefix = args.prefix.resolve()
components = json.loads(args.components.read_text())
inventory = json.loads(args.inventory.read_text())
abi = {}
modules = [pattern for item in inventory['components'].values()
           for pattern in item['package'].get('modules', [])]
for library in sorted((prefix / 'lib').rglob('*')):
    if library.is_symlink() or not library.is_file() or not re.search(r'\.so(?:\.|$)|\.dylib$', library.name):
        continue
    name = library.relative_to(prefix).as_posix()
    module = any(fnmatch.fnmatchcase(name, pattern) for pattern in modules)
    if args.target.endswith('darwin'):
        header = subprocess.check_output([args.tool, '-hv', str(library)], text=True)
        names = subprocess.check_output([args.tool, '-D', str(library)], text=True).splitlines()[1:]
        links = subprocess.check_output([args.tool, '-L', str(library)], text=True).splitlines()[1:]
        if names and links:
            abi[name] = dict(install_name=names[0].strip(), compatibility=links[0].strip())
        else:
            if not module or not re.search(r'\bBUNDLE\b', header):
                raise ValueError('Missing install name: ' + name)
            abi[name] = dict(kind='module', install_name=None, compatibility=None, loader_name=library.name)
    else:
        output = subprocess.check_output([args.tool, '-d', str(library)], text=True)
        match = re.search(r'\(SONAME\).*?\[(.*?)\]', output)
        if match:
            abi[name] = dict(soname=match[1])
        else:
            if not module:
                raise ValueError('Missing SONAME: ' + name)
            abi[name] = dict(kind='module', soname=None, loader_name=library.name)
for component in components:
    package = inventory['components'][component['name']]['package']
    patterns = package['owned_patterns'] + [
        'lib/lib' + facade.get('output_name', facade['target']) + '.*'
        for facade in package['facades']]
    component['abi'] = {name: value for name, value in abi.items()
                        if any(fnmatch.fnmatchcase(name, pattern) for pattern in patterns)}
    if not component['abi']:
        if package['facades']:
            raise ValueError('Missing component ABI: ' + component['name'])
        component['abi'] = dict(shared_surface='header-only')
docs = prefix / 'share/doc/cpkt/core'
docs.mkdir(parents=True, exist_ok=True)
(docs / 'THIRD_PARTY_NOTICES.md').write_text('Bundled core components and complete license paths:\n\n' + ''.join(
    '- ' + item['name'] + ' ' + item['version'] + ': third_party/' + item['name'] + '/LICENSE\n'
    for item in components))
(docs / 'README.md').write_text('cpkt ' + args.version + ' core SDK\n\nIndependently usable core.\n')
files = []
for path in sorted(prefix.rglob('*')):
    name = path.relative_to(prefix).as_posix()
    if name.startswith('share/cpkt/packages/') or path.is_dir() and not path.is_symlink():
        continue
    if path.is_symlink():
        target = os.readlink(path)
        if Path(target).is_absolute() or not (path.parent / target).resolve().is_relative_to(prefix):
            raise ValueError('Escaping SDK link: ' + name)
        files.append(dict(path=name, type='symlink', target=target))
    else:
        if not stat.S_ISREG(path.stat().st_mode):
            raise ValueError('Special SDK file: ' + name)
        files.append(dict(path=name, type='file', mode=format(stat.S_IMODE(path.stat().st_mode), '04o'), sha256=sha(path)))
manifest = dict(schema_version=1, group='core', release_version=args.version, target_id=args.target,
                libc=None if args.target.endswith('darwin') else args.target.split('-')[-1],
                macos_deployment_target=args.floor if args.target.endswith('darwin') else None,
                components=sorted(components, key=lambda item: item['name']), files=files, requires_core=None)
manifest['package_id'] = hashlib.sha256(canonical(manifest)).hexdigest()
destination = prefix / 'share/cpkt/packages/core.json'
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_bytes(canonical(manifest))
destination.chmod(0o644)
