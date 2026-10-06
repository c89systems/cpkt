#!/usr/bin/env python3
"""Validate prerequisites and revoke attempted configure proof before project()."""
import argparse
import os
import re
from pathlib import Path
import sys
from cpkt_lock import delegated
from cpkt_receipts import cache, validate_core

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--group', required=True)
parser.add_argument('--target', default='')
parser.add_argument('--arch', default='')
parser.add_argument('--os', default='')
parser.add_argument('--libc', default='')
parser.add_argument('--configuration', default='')
parser.add_argument('--prerequisite', default='')
parser.add_argument('--producer', default='OFF')
args = parser.parse_args()
try:
    root = args.root.resolve()
    delegated(root, args.group)
    if args.binary.is_relative_to(root/'build'):
        parts = args.binary.relative_to(root/'build').parts
        if len(parts) >= 3 and parts[1] in ('core','db','misc','all'):
            if parts[1] != args.group:
                raise RuntimeError('managed binary directory is owned by '+parts[1]+', not '+args.group)
            if (parts[2] == 'producer') != (args.producer == 'ON'):
                raise RuntimeError('producer and consumer graph directories cannot be interchanged')
    if args.producer == 'ON' and args.group == 'all':
        raise RuntimeError('a producer graph owns one shipped group; use scripts/build.sh deps --group all')
    previous = cache(args.binary/'CMakeCache.txt') if (args.binary/'CMakeCache.txt').is_file() else {}
    target = args.target or os.environ.get('CPKT_RESOLVED_TARGET') or previous.get('CPKT_TARGET_ID','')
    if not target and args.arch and args.os:
        arch = {'aarch64':'arm64'}.get(args.arch,args.arch) if args.os.lower() == 'darwin' else args.arch
        target = arch + ('-apple-darwin' if args.os.lower() == 'darwin' else '-linux-'+args.libc)
    configuration = args.configuration or previous.get('CMAKE_BUILD_TYPE','Debug')
    if target and not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*',target):
        raise RuntimeError('invalid resolved SDK target identity')
    if args.binary.is_relative_to(root/'build'):
        parts = args.binary.relative_to(root/'build').parts
        if len(parts) >= 3 and parts[1] in ('core','db','misc','all') and parts[0] != target:
            raise RuntimeError('managed binary directory target does not match '+target)
    if args.group in ('db','misc'):
        if not target:
            raise RuntimeError('cannot resolve core prerequisite before configure; use scripts/build.sh configure --group '+args.group+' --preset debug')
        validate_core(root,target,args.prerequisite or configuration,os.environ.get('CPKT_PRESET','debug'))
    if target and args.producer != 'ON':
        directory = root/'build/verification'/target/args.group
        receipt_configuration = configuration
        if args.binary.is_relative_to(root/'build'/target/args.group):
            receipt_configuration = args.binary.name
        names = ('*-development.json','*-built.json') if args.producer == 'ON' else (
            receipt_configuration+'-development.json',receipt_configuration+'-built.json')
        for name in names:
            for path in directory.glob(name):
                path.unlink(missing_ok=True)
except (RuntimeError,OSError,ValueError) as error:
    sys.exit('configure prerequisite guard: '+str(error))
