#!/usr/bin/env python3
"""Validate exact JUnit/dashboard coverage and memory-defect records."""
import argparse
from collections import Counter
import json
from pathlib import Path
import shutil
import sys
import xml.etree.ElementTree as ET
from cpkt_inventory import components_for, load
from cpkt_lock import delegated
from cpkt_receipts import cache, file_identity, group_outputs, publish, validate_component, verification_inputs, mutation_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('inventory', 'tested'))
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--group', required=True)
    parser.add_argument('--target', required=True)
    parser.add_argument('--configuration', required=True)
    parser.add_argument('--preset', required=True)
    parser.add_argument('--regex', default='')
    args = parser.parse_args()
    delegated(args.root, args.group)
    directory = args.root/'build'/args.target/args.group/args.configuration
    proof = mutation_path(args.root/'build/verification'/args.target/args.group/(args.configuration+('-memcheck-focused.json' if args.regex else '-memcheck.json')))
    tests = json.loads((directory/'cpkt-memcheck-inventory.json').read_text())['tests']
    if not tests:
        raise RuntimeError('Memcheck has no selected cases')
    proof.unlink(missing_ok=True)
    if args.action == 'inventory':
        return
    cases = list(ET.parse(directory/'cpkt-memcheck-results.xml').getroot().iter('testcase'))
    if Counter(case.attrib['name'] for case in cases) != Counter(item['name'] for item in tests):
        raise RuntimeError('Memcheck JUnit has missing or duplicate cases')
    if any(case.find(tag) is not None for case in cases for tag in ('failure', 'error', 'skipped')):
        raise RuntimeError('Memcheck has failed/skipped cases')
    tag = (directory/'Testing/TAG').read_text().splitlines()[0]
    memory = ET.parse(directory/'Testing'/tag/'DynamicAnalysis.xml').getroot()
    if any(int(item.text or '0') for item in memory.iter('Defect')):
        raise RuntimeError('Valgrind reported memory defects')
    configured = cache(directory/'CMakeCache.txt')
    publish(proof, {'kind': 'memcheck', 'group': args.group, 'target': args.target,
                    'configuration': args.configuration, 'profile': 'native-memcheck',
                    'coverage': [case.attrib['name'] for case in cases], 'selection': args.regex,
                    'verification_id': verification_inputs(args.root, args.group, configured),
                    'outputs': group_outputs(directory),
                    'valgrind': file_identity(Path(shutil.which('valgrind')).resolve()),
                    'components': {name: validate_component(args.root, args.target, name)['input_id']
                                   for name in components_for(load(args.root), args.group, True)}})


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError, ET.ParseError) as error:
        sys.exit('Memcheck evidence: '+str(error))
