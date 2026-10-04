#!/usr/bin/env python3
"""Validate selected installed SDK bytes without changing the prefix (stdlib only)."""
import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys

GROUPS = ('core', 'db', 'misc')
FIELDS = {'schema_version', 'group', 'release_version', 'target_id', 'libc',
          'macos_deployment_target', 'components', 'files', 'package_id', 'requires_core'}


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('utf-8')


def sha(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1048576), b''):
            value.update(chunk)
    return value.hexdigest()


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON key: ' + key)
        result[key] = value
    return result


def decode(payload):
    return json.loads(payload.decode('utf-8'), object_pairs_hook=unique,
                      parse_float=lambda _: (_ for _ in ()).throw(ValueError('floating JSON value')),
                      parse_constant=lambda _: (_ for _ in ()).throw(ValueError('nonfinite JSON value')))


def path_name(value):
    if not isinstance(value, str) or not value or '\\' in value or '\x00' in value:
        raise ValueError('invalid payload path')
    path = PurePosixPath(value)
    if path.is_absolute() or path.as_posix() != value or any(p in ('', '.', '..') for p in value.split('/')):
        raise ValueError('non-normalized payload path: ' + value)
    return value


def hash_string(value):
    if not isinstance(value, str) or re.fullmatch('[0-9a-f]{64}', value) is None:
        raise ValueError('invalid SHA-256 identity')


def load_manifest(prefix, group):
    path = prefix / ('share/cpkt/packages/' + group + '.json')
    if path.is_symlink() or not path.is_file():
        raise ValueError('missing regular manifest for ' + group)
    if stat.S_IMODE(path.stat().st_mode)!=0o644:raise ValueError('manifest mode must be 0644: '+group)
    raw = path.read_bytes()
    item = decode(raw)
    if not isinstance(item, dict) or set(item) != FIELDS or type(item['schema_version']) is not int or item['schema_version'] != 1:
        raise ValueError('unsupported manifest schema or unknown/missing fields: ' + group)
    if raw != canonical(item):
        raise ValueError('manifest is not canonical JSON: ' + group)
    if item['group'] != group or not isinstance(item['release_version'], str) or not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', item['release_version']):
        raise ValueError('invalid group/version: ' + group)
    target = item['target_id']
    if target not in ('x86_64-linux-gnu', 'x86_64-linux-musl', 'aarch64-linux-gnu', 'aarch64-linux-musl', 'armhf-linux-gnu', 'armhf-linux-musl', 'arm64-apple-darwin'):
        raise ValueError('unsupported target: ' + str(target))
    darwin = target == 'arm64-apple-darwin'
    if item['libc'] != (None if darwin else target.split('-')[-1]):
        raise ValueError('target/libc mismatch')
    floor = item['macos_deployment_target']
    if (darwin and (not isinstance(floor, str) or not re.fullmatch(r'[0-9]+\.[0-9]+', floor))) or (not darwin and floor is not None):
        raise ValueError('invalid Darwin deployment floor')
    hash_string(item['package_id'])
    unsigned = dict(item)
    del unsigned['package_id']
    if hashlib.sha256(canonical(unsigned)).hexdigest() != item['package_id']:
        raise ValueError('package identity mismatch: ' + group)
    names = []
    for component in item['components']:
        if not isinstance(component, dict) or set(component) != {'name', 'version', 'source_sha256', 'features', 'abi'}:
            raise ValueError('unknown component fields')
        if not all(isinstance(component[k], str) and component[k] for k in ('name', 'version')):
            raise ValueError('invalid component name/version')
        hash_string(component['source_sha256'])
        if not isinstance(component['features'], dict) or not isinstance(component['abi'], dict) or not component['abi']:
            raise ValueError('missing component features/ABI')
        names.append(component['name'])
    if names != sorted(set(names)) or not names:
        raise ValueError('unsorted/duplicate/empty components')
    requirement = item['requires_core']
    if group == 'core':
        if requirement is not None:
            raise ValueError('core requires optional payload')
    elif not isinstance(requirement, dict) or set(requirement) != {'package_id', 'release_version', 'target_id'}:
        raise ValueError('invalid core requirement: ' + group)
    return item


def walk(prefix):
    for root, directories, files in os.walk(prefix, followlinks=False):
        for name in sorted(directories + files):
            path = Path(root) / name
            if path.is_symlink() or not path.is_dir():
                yield path.relative_to(prefix).as_posix(), path


def validate(prefix, groups, expected_version=None, expected_target=None, expected_ids=None, cmake_summary=False):
    prefix = Path(prefix).absolute()
    if prefix.is_symlink() or not prefix.is_dir():
        raise ValueError('SDK prefix must be a real directory')
    selected = set(groups)
    groups = set(groups)
    if not groups or not groups <= set(GROUPS):
        raise ValueError('unknown selected groups')
    groups.add('core')
    manifests = {group: load_manifest(prefix, group) for group in sorted(groups)}
    core = manifests['core']
    for group, item in manifests.items():
        for key in ('target_id', 'libc', 'macos_deployment_target'):
            if item[key] != core[key]:
                raise ValueError('composition ' + key + ' mismatch: ' + group)
        if group != 'core' and item['requires_core'] != {k: core[k] for k in ('package_id', 'release_version', 'target_id')}:
            raise ValueError('exact core package requirement mismatch: ' + group)
        if expected_ids and group in expected_ids and expected_ids[group] != item['package_id']:
            raise ValueError('expected package ID mismatch: ' + group)
    if expected_version is not None and any(manifests[g]['release_version'] != expected_version for g in selected if g != 'core' or selected == {'core'}):
        raise ValueError('expected release version mismatch')
    if expected_target is not None and core['target_id'] != expected_target:
        raise ValueError('expected target mismatch')
    owned = {}
    library_names=set()
    for group, item in manifests.items():
        paths = []
        for entry in item['files']:
            name = path_name(entry['path'])
            paths.append(name)
            if cmake_summary and re.fullmatch(r"lib/[^/]+\.(?:a|so(?:\.[0-9]+)*|(?:[0-9]+\.)*dylib)",name):library_names.add(PurePosixPath(name).name)
            if name in owned or name in {'share/cpkt/packages/' + g + '.json' for g in GROUPS}:
                raise ValueError('payload collision/self-inclusion: ' + name)
            owned[name] = group
            path = prefix / name
            for parent in path.parents:
                if parent == prefix:
                    break
                if parent.is_symlink():
                    raise ValueError('symlink payload ancestor: ' + name)
            info = path.lstat()
            if entry['type'] == 'file':
                if set(entry) != {'path', 'type', 'mode', 'sha256'} or not re.fullmatch('[0-7]{4}', entry['mode']):
                    raise ValueError('unknown/invalid regular file fields: ' + name)
                hash_string(entry['sha256'])
                if not stat.S_ISREG(info.st_mode) or format(stat.S_IMODE(info.st_mode), '04o') != entry['mode'] or sha(path) != entry['sha256']:
                    raise ValueError('file content/mode mismatch: ' + name)
            elif entry['type'] == 'symlink':
                if set(entry) != {'path', 'type', 'target'} or not stat.S_ISLNK(info.st_mode) or os.readlink(path) != entry['target']:
                    raise ValueError('symlink mismatch: ' + name)
                link = entry['target']
                if not isinstance(link, str) or not link or '\\' in link or PurePosixPath(link).is_absolute():
                    raise ValueError('absolute/invalid symlink: ' + name)
                try:
                    resolved = path.resolve(strict=True).relative_to(prefix.resolve()).as_posix()
                except (ValueError, RuntimeError, OSError) as error:
                    raise ValueError('dangling/escaping symlink: ' + name) from error
                # Check closure after the whole declared inventory has been read.
            else:
                raise ValueError('unsupported file type: ' + name)
        if paths != sorted(set(paths)):
            raise ValueError('unsorted/duplicate file inventory: ' + group)
    for name in owned:
        path = prefix / name
        if path.is_symlink() and path.resolve().relative_to(prefix.resolve()).as_posix() not in owned:
            raise ValueError('symlink reaches an unselected payload: ' + name)
    # Unselected optional bytes do not participate in this closure. Their paths
    # are identified by the core-owned declarative catalog, never their hashes.
    catalog_path = prefix / 'share/cpkt/payload-ownership.json'
    catalog = decode(catalog_path.read_bytes()) if catalog_path.is_file() else {}
    unselected = [p for g, paths in catalog.items() if g not in groups for p in paths]
    for name, path in walk(prefix):
        if name in owned or name in {'share/cpkt/packages/' + g + '.json' for g in GROUPS}:
            continue
        if any(fnmatch.fnmatchcase(name, pattern) for pattern in unselected):
            continue
        raise ValueError('extra installed payload: ' + name)
    ids={g:m['package_id'] for g,m in manifests.items()}
    return {'package_ids':ids,'library_names':';'.join(sorted(library_names))} if cmake_summary else ids


def consumer_platform(core,arch=None,os_name=None,libc=None,system=None,cpu=None):
    target=core['target_id']
    sdk_arch=target.split('-')[0]
    sdk_os='darwin' if target.endswith('darwin') else 'linux'
    aliases={'x86_64':'x86_64','amd64':'x86_64','arm64':'arm64','aarch64':'arm64' if sdk_os=='darwin' else 'aarch64','arm':'armhf','armv7':'armhf','armv7l':'armhf','armhf':'armhf'}
    cpus=[cpu] if isinstance(cpu,str) else cpu or []
    for value in [arch]+cpus:
        if value and aliases.get(value.lower(),value.lower())!=sdk_arch:
            raise ValueError('consumer CPU/architecture mismatch: '+value+' versus '+sdk_arch)
    for value in (os_name,system):
        if value and value.lower()!=sdk_os:raise ValueError('consumer platform mismatch: '+value+' versus '+sdk_os)
    if libc and libc!=core['libc']:raise ValueError('consumer libc mismatch')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--groups', required=True)
    parser.add_argument('--version')
    parser.add_argument('--target')
    for name in ('arch','os','libc','system'):parser.add_argument('--'+name)
    parser.add_argument('--cpu',action='append')
    parser.add_argument('--cmake-summary',action='store_true')
    parser.add_argument('--package-id', action='append', default=[])
    args = parser.parse_args()
    groups = GROUPS if args.groups == 'all' else args.groups.split(',')
    ids = dict(item.split('=', 1) for item in args.package_id)
    result = validate(args.prefix, groups, args.version, args.target, ids, args.cmake_summary)
    consumer_platform(load_manifest(args.prefix,'core'),args.arch,args.os,args.libc,args.system,args.cpu)
    print(canonical(result).decode())


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError, RecursionError) as error:
        sys.exit('SDK validation failed: ' + str(error))
