#!/usr/bin/env python3
"""Validate a selected archive's structure/bytes before installed consumers."""
import argparse
import os
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import signal
from contextlib import contextmanager
from cpkt_packages import ROOT, safe_owned, safe_extract, prefix_name, validator, prepared_core, command, version
from cpkt_lock import delegated, ensure_operation as locked_run

def main():
    values={}
    args=[]
    for word in sys.argv[1:]:
        if word.startswith('-D') and '=' in word:
            key,value=word[2:].split('=',1);values[key]=value
        else:args.append(word)
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive',type=Path,default=values.get('CPKT_ARCHIVE'))
    parser.add_argument('--target',default=values.get('CPKT_TARGET_ID'))
    parser.add_argument('--version',default=values.get('CPKT_BUNDLE_VERSION'))
    parser.add_argument('--group',choices=['core','db','misc'],default=values.get('CPKT_GROUP',os.environ.get('GROUP','core') if os.environ.get('GROUP')!='all' else 'core'))
    parser.add_argument('--preset');parser.add_argument('--consumers',action='store_true')
    selected=parser.parse_args(args)
    if not selected.archive or not selected.target or not selected.version:parser.error('archive, target and version are required')
    if 'CPKT_OPERATION_FD' not in os.environ:locked_run(ROOT,selected.group)
    delegated(ROOT,selected.group)
    expected=prefix_name(selected.version,selected.target)
    # Reject obsolete test-only payloads even if a forged manifest declares them.
    with tarfile.open(selected.archive,'r:gz') as stream:
        for member in stream:
            if member.name.rstrip('/').split('/')[0]!=expected or '..' in member.name.split('/'):
                raise ValueError('package archive contains entry outside its root: '+member.name)
            if '/pslog' in member.name or '/libpslog' in member.name:raise ValueError('release archive must not contain test-only libpslog')
    parent=safe_owned(ROOT/'build/package-assertions')
    existed=parent.exists();parent.mkdir(parents=True,exist_ok=True)
    workspace=Path(tempfile.mkdtemp(prefix='assertion.',dir=parent))
    handlers={}
    def interrupted(signum,frame):raise SystemExit(128+signum)
    try:
        for signum in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP):
            handlers[signum]=signal.signal(signum,interrupted)
        groups=['core']
        if selected.group!='core':
            archive,_=prepared_core(selected.version,selected.target,selected.preset or selected.target+'-release')
            safe_extract(archive,workspace,expected);groups.append(selected.group)
        prefix=safe_extract(selected.archive,workspace,expected)
        validator.validate(prefix,groups,selected.version,selected.target)
        if selected.consumers:
            command([sys.executable,ROOT/'scripts/cpkt_sdk_consumer.py','--prefix',prefix,'--target',selected.target,'--groups',','.join(groups),'--owners',selected.group,'--preset',selected.preset or selected.target+'-release'],group=selected.group)
    finally:
        for signum,handler in handlers.items():signal.signal(signum,handler)
        shutil.rmtree(workspace)
        if not existed:
            try:parent.rmdir()
            except OSError:pass
    print('selected archive assertions passed')
if __name__=='__main__':
    try:sys.exit(main())
    except (ValueError,RuntimeError,OSError,KeyError) as error:sys.exit(str(error))
