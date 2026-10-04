#!/usr/bin/env python3
"""Validate an exact binary/release payload inventory, with one manifest owner."""
import argparse
from pathlib import Path
import sys
from cpkt_packages import ROOT, check_snapshot, artifacts, source_proof
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory',type=Path);parser.add_argument('project',choices=['cpkt']);parser.add_argument('version');parser.add_argument('--scope',choices=['binary','release'],default='release');parser.add_argument('--manifest',type=Path)
args=parser.parse_args()
try:
    manifest=args.manifest or (args.directory/f'cpkt-{args.version}-CHECKSUMS' if args.scope=='release' else ROOT/'build/verification/binary'/args.version/'CHECKSUMS')
    check_snapshot(manifest,args.directory,args.version,args.scope,current_run=False)
    expected=set(artifacts(args.version,args.scope))|{manifest.name}
    if args.scope=='binary':expected|={f'cpkt-{args.version}.tar.gz',f'cpkt-{args.version}-CHECKSUMS'}
    extra={p.name for p in args.directory.iterdir() if p.is_file()}-expected
    if extra:raise ValueError('unexpected distribution artifact: '+','.join(sorted(extra)))
    print('verified exact '+args.scope+' inventory')
except (ValueError,RuntimeError,OSError,KeyError) as error:sys.exit(str(error))
