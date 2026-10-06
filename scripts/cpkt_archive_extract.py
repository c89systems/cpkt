#!/usr/bin/env python3
"""Preflight archive paths, links, ownership and destinations before extraction."""
import argparse
from pathlib import Path
import sys
import tarfile
from cpkt_packages import ROOT, safe_extract
from cpkt_lock import delegated, ensure_operation as locked_run
import os
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('archive',type=Path);parser.add_argument('destination',type=Path);parser.add_argument('root');parser.add_argument('--source',action='store_true')
args=parser.parse_args()
if "CPKT_OPERATION_FD" not in os.environ:
 locked_run(ROOT,"all")
delegated(ROOT,"all")
try:
 if args.source:
  with tarfile.open(args.archive,'r:gz') as stream:
   directories={m.name.rstrip('/').split('/')[0] for m in stream.getmembers() if m.isdir()}
   if len(directories)!=1:raise ValueError('source archive must contain exactly one root directory')
   for member in stream.getmembers():
    if member.name.rstrip("/").split("/")[0] != args.root:
     if len(directories)==1 and next(iter(directories)) != args.root:continue
     raise ValueError("source archive contains entry outside its root: "+member.name)
   actual=next(iter(directories))
   if actual!=args.root:raise ValueError('source archive root is '+actual+', expected '+args.root)
 safe_extract(args.archive,args.destination,args.root)
except (ValueError,OSError,RuntimeError) as error:sys.exit(str(error))
