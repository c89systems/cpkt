#!/usr/bin/env python3
"""Reserved version-contract ref ownership and interruption-safe compare-delete."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import secrets
import tempfile
import sys

from cpkt_operation import delegated, run as locked_run

REF='refs/tags/v99.99.99'


def git(root,*args,check=True):
    result=subprocess.run(['git','-C',str(root),*args],capture_output=True,text=True)
    if check and result.returncode:raise ValueError('git operation failed: '+result.stderr.strip())
    return result.stdout.strip() if result.returncode==0 else None


def record_path(root):
    root=Path(os.path.abspath(root))
    path=root/'build/control/reserved-tag.json'
    # Check ancestry even for missing records and before creating directories.
    for parent in (path,*path.parents):
        if parent.is_symlink():raise ValueError('reserved-tag recovery path is a symlink')
    return path


def owned_creation(root,data):
    logfile=Path(git(root,'rev-parse','--git-path','logs/'+REF))
    if not logfile.is_absolute():logfile=root/logfile
    if any(p.is_symlink() for p in (logfile,*logfile.parents)) or not logfile.is_file():return False
    lines=logfile.read_text().splitlines()
    if not lines:return False
    fields=lines[-1].split('\t',1)
    oids=fields[0].split(' ',2)
    return len(fields)==2 and len(oids)==3 and oids[:2]==['0'*len(data['oid']),data['oid']] and fields[1]=='cpkt-reserved-tag:'+data['nonce']


def recover(root):
    path=record_path(root);oid=git(root,'show-ref','--verify','--hash',REF,check=False)
    if not path.exists():
        if oid:raise ValueError('reserved v99.99.99 exists without an ownership recovery record; refusing deletion')
        return
    from cpkt_packages import validator
    data=validator.decode(path.read_bytes())
    if set(data)!={'schema_version','root','ref','oid','state','nonce'} or type(data['schema_version']) is not int or data['schema_version']!=1 or data['root']!=str(root.resolve()) or data['ref']!=REF or data['state'] not in ('prepared','created') or not re.fullmatch('[0-9a-f]{40}|[0-9a-f]{64}',data['oid']) or not re.fullmatch('[0-9a-f]{64}',data['nonce']):raise ValueError('invalid reserved-tag recovery record')
    if oid and (oid!=data['oid'] or git(root,'cat-file','-t',oid)!='commit' or not owned_creation(root,data)):
        raise ValueError('reserved ref ownership lost; refusing deletion')
    if oid:git(root,'update-ref','-d',REF,data['oid'])
    path.unlink()


def persist(path,data):
    record_path(Path(data['root']))
    fd,name=tempfile.mkstemp(prefix='reserved-tag-',suffix='.tmp',dir=path.parent)
    temporary=Path(name)
    try:
        with os.fdopen(fd,'w') as output:
            json.dump(data,output,sort_keys=True);output.flush();os.fsync(output.fileno())
        temporary.replace(path)
    finally:temporary.unlink(missing_ok=True)


def create(root):
    path=record_path(root)
    recover(root)
    oid=git(root,'rev-parse','HEAD')
    data={'schema_version':1,'root':str(root.resolve()),'ref':REF,'oid':oid,'state':'prepared','nonce':secrets.token_hex(32)}
    path.parent.mkdir(parents=True,exist_ok=True)
    record_path(root)
    persist(path,data)
    git(root,'update-ref','--create-reflog','-m','cpkt-reserved-tag:'+data['nonce'],REF,oid,'0'*len(oid))
    data['state']='created';persist(path,data)
    return oid


def check(root):
    recover(root)
    tags=git(root,'tag','--points-at','HEAD','--list','v[0-9]*.[0-9]*.[0-9]*').splitlines()
    tags=[tag for tag in tags if re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+',tag)]
    if tags:
        tag=max(tags,key=lambda x:tuple(map(int,x[1:].split('.'))))
        if git(root,'cat-file','-t','refs/tags/'+tag)!='commit':raise ValueError('exact HEAD version tag must be lightweight')
        expected=tag[1:]
    else:
        expected='0.0.0'
    def assert_version(value):
        actual=subprocess.check_output(['bash',str(root/'scripts/release-version.sh'),str(root)],text=True).strip()
        public=subprocess.check_output(['make','-s','-C',str(root),'print-release-version'],text=True).strip()
        if actual!=value or public!=value:raise ValueError('release version precedence mismatch')
    assert_version(expected)
    if not tags:
        create(root)
        try:assert_version('99.99.99')
        finally:recover(root)
        assert_version('0.0.0')
    print('release version contract passed; exact HEAD precedence and reserved ref recovery verified')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['check','recover']);parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parent.parent)
    args=parser.parse_args();root=Path(os.path.abspath(args.root));record_path(root)
    if 'CPKT_OPERATION_FD' not in os.environ:return locked_run(root,'all',[sys.executable,__file__]+sys.argv[1:])
    delegated(root,'all')
    (check if args.action=='check' else recover)(root)


if __name__=='__main__':
    try:sys.exit(main())
    except (ValueError,OSError,RuntimeError) as error:sys.exit('version contract: '+str(error))
