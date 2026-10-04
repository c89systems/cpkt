#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path
from cpkt_inventory import GROUPS, load, components_for

parser = argparse.ArgumentParser()
parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
parser.add_argument('--group', choices=GROUPS, default=os.environ.get('GROUP','all'))
parser.add_argument('--closure', action='store_true')
parser.add_argument('--cmake', action='store_true')
parser.add_argument('--no-tests', action='store_true')
args = parser.parse_args()
if args.group not in GROUPS:
    parser.error('unknown GROUP: '+args.group)
data = load(args.root)
items = components_for(data, args.group, args.closure, not args.no_tests)
print(';'.join(items) if args.cmake else json.dumps({'group':args.group,'components':items}))
