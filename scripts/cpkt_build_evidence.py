#!/usr/bin/env python3
"""Validate output inventories and structured CTest/JUnit completion evidence.

This helper reads records and publishes verified facts. Bash runs all commands;
CMake owns the graph. JSON, XML, file modes, symlinks and exact test inventories
share the SDK validator's portable content encoding rather than shell text parsing.
"""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

from cpkt_inventory import components_for, load, record
from cpkt_lock import delegated
from cpkt_receipts import (cache, group_outputs, publish, read, readiness_path,
                          validate_component, validate_development, verification_inputs, mutation_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('validate', 'before', 'built', 'restore', 'inventory', 'tested'))
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--group', required=True)
    parser.add_argument('--target', required=True)
    parser.add_argument('--configuration', required=True)
    parser.add_argument('--preset', required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    delegated(root, args.group)
    directory = root/'build'/args.target/args.group/args.configuration
    ready = readiness_path(root, args.target, args.group, args.configuration)
    previous = root/'build/control/readiness'/os.environ['CPKT_OPERATION_RUN']/args.target/args.group/(args.configuration+'.json')
    if args.action == 'validate':
        validate_development(root, args.target, args.group, args.configuration, args.preset)
        return
    ready = mutation_path(ready)
    built = mutation_path(ready.parent/(args.configuration+'-built.json'))
    previous = mutation_path(previous)
    if args.action == 'before':
        previous.unlink(missing_ok=True)
        try:
            prior = validate_development(root, args.target, args.group, args.configuration, args.preset)
        except (RuntimeError, OSError, KeyError):
            prior = None
        if prior is not None:
            publish(previous, prior, preserve_run=True)
        ready.unlink(missing_ok=True)
        built.unlink(missing_ok=True)
        return
    configured = cache(directory/'CMakeCache.txt')
    if args.action in ('built', 'restore'):
        if args.action == 'restore' and not previous.is_file():
            return
        try:
            outputs = group_outputs(directory)
        except (RuntimeError, OSError):
            if args.action == 'restore': return
            raise
        if args.action == 'built':
            publish(built,
                    {'kind': 'built', 'target': args.target, 'group': args.group,
                     'configuration': args.configuration, 'outputs': outputs, 'coverage': []})
        if previous.is_file():
            prior = read(previous)
            if (prior['outputs'] == outputs
                    and prior['verification_id'] == verification_inputs(root, args.group, configured)
                    and prior['components'] == {name: validate_component(root, args.target, name)['input_id']
                                               for name in components_for(load(root), args.group, True)}):
                publish(ready, prior, preserve_run=True)
        return
    ready.unlink(missing_ok=True)
    listing = json.loads((directory/'cpkt-test-inventory.json').read_text())['tests']
    if not listing:
        raise RuntimeError('required CTest inventory is empty')
    names = [item['name'] for item in listing]
    required = (directory/'cpkt-required-coverage.txt').read_text().splitlines()
    if Counter(names) != Counter(required):
        raise RuntimeError('CTest inventory differs from required native coverage')
    if len(names) != len(set(names)):
        raise RuntimeError('duplicate CTest case')
    owned = set((directory/'cpkt-owned-outputs.txt').read_text().splitlines())
    for item in listing:
        if record(load(root)['tests'], item['name'])['group'] not in (args.group, 'tooling', 'all'):
            raise RuntimeError('cross-owner test: '+item['name'])
        properties = {entry['name']: entry['value'] for entry in item.get('properties', [])}
        if properties.get('DISABLED'):
            raise RuntimeError('required test disabled: '+item['name'])
        command = item.get('command', [])
        if not command:
            raise RuntimeError('required test has no command: '+item['name'])
        for argument in command:
            path = Path(argument)
            if path.is_absolute() and path.is_relative_to(directory) and (argument == command[0] or argument in owned) and not path.exists():
                raise RuntimeError('required test executable absent: '+argument)
    if args.action == 'inventory':
        return
    cases = list(ET.parse(directory/'cpkt-test-results.xml').getroot().iter('testcase'))
    if Counter(case.attrib['name'] for case in cases) != Counter(names):
        raise RuntimeError('JUnit is missing or duplicating required CTest cases')
    if any(case.find(tag) is not None for case in cases for tag in ('failure', 'error', 'skipped')):
        raise RuntimeError('failed/skipped cases cannot supply tested readiness')
    profile = ('native-darwin' if sys.platform == 'darwin' else 'osxcross') if args.target.endswith('darwin') else (
        'native-linux' if args.target == 'x86_64-linux-gnu' else 'linux-runner')
    publish(ready, {'kind': 'development', 'target': args.target, 'group': args.group,
                   'configuration': args.configuration, 'profile': profile,
                   'verification_id': verification_inputs(root, args.group, configured),
                   'components': {name: validate_component(root, args.target, name)['input_id']
                                  for name in components_for(load(root), args.group, True)},
                   'outputs': group_outputs(directory), 'coverage': names,
                   'deferred': ['native-runtime-required-release'] if profile == 'osxcross' else []})


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError, ET.ParseError) as error:
        sys.exit('build evidence: '+str(error))
