#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import sys
from cpkt_lock import delegated
from cpkt_inventory import load
from cpkt_receipts import validate_component, validate_core, publish_component

parser = argparse.ArgumentParser()
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--group', required=True)
parser.add_argument('--target', required=True)
parser.add_argument('--component')
parser.add_argument('--publish', action='store_true')
parser.add_argument('--configuration', default='Debug')
parser.add_argument('--preset', default=os.environ.get('CPKT_PRESET', 'debug'))
args = parser.parse_args()
try:
    delegated(args.root, args.group)
    if args.component:
        if args.publish:
            if load(args.root)['components'][args.component]['group'] != args.group:
                raise RuntimeError('cannot publish another group producer')
            publish_component(args.root, args.target, args.component)
        else:
            validate_component(args.root, args.target, args.component)
    else:
        validate_core(args.root, args.target, args.configuration, args.preset)
except (RuntimeError, OSError, ValueError, KeyError) as error:
    sys.exit(str(error) + '\nRepair: make test GROUP=' + args.group + ' PRESET=' + args.preset)
