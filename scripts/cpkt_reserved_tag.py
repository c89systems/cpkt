#!/usr/bin/env python3
"""Validate atomic reserved-ref ownership records; Bash performs Git operations."""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import tempfile
from cpkt_lock import delegated
from cpkt_packages import validator

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=('prepare','created','inspect','clear'))
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--oid',default='')
    parser.add_argument('--nonce')
    parser.add_argument('--log',type=Path)
    args=parser.parse_args()
    root=args.root.resolve()
    delegated(root,'all')
    path=root/'build/control/reserved-tag.json'
    if any(item.is_symlink() for item in (path,*path.parents)):
        raise ValueError('reserved-ref record has a symlink ancestor')
    if args.action=='prepare':
        if path.exists():raise ValueError('reserved-ref record must be recovered first')
        if not re.fullmatch('[0-9a-f]{40}|[0-9a-f]{64}',args.oid) or not re.fullmatch('[0-9a-f]{64}',args.nonce or ''):
            raise ValueError('invalid reserved-ref identity')
        data={'schema_version':1,'root':str(root),'ref':'refs/tags/v99.99.99','oid':args.oid,'nonce':args.nonce,'state':'prepared'}
    elif not path.exists():
        if args.oid:raise ValueError('reserved ref exists without an ownership record')
        return
    else:
        data=validator.decode(path.read_bytes())
        if (set(data)!={'schema_version','root','ref','oid','state','nonce'} or type(data['schema_version']) is not int
                or data['schema_version']!=1 or data['root']!=str(root) or data['ref']!='refs/tags/v99.99.99'
                or data['state'] not in ('prepared','created')
                or not re.fullmatch('[0-9a-f]{40}|[0-9a-f]{64}',data['oid'])
                or not re.fullmatch('[0-9a-f]{64}',data['nonce'])):
            raise ValueError('invalid reserved-ref ownership record')
        if args.oid:
            if args.oid!=data['oid'] or not args.log or not args.log.is_file() or any(p.is_symlink() for p in (args.log,*args.log.parents)):
                raise ValueError('reserved ref ownership lost')
            entry=args.log.read_text().splitlines()[-1].split('\t',1)
            fields=entry[0].split(' ',2)
            if len(entry)!=2 or fields[:2]!=['0'*len(data['oid']),data['oid']] or entry[1]!='cpkt-reserved-tag:'+data['nonce']:
                raise ValueError('reserved creation is not the recorded reflog entry')
        if args.action=='inspect':
            print(data['oid'])
            return
        if args.action=='clear':
            if args.oid:raise ValueError('reserved-ref record cannot be cleared before ref deletion')
            path.unlink()
            return
        if args.action=='created':
            if not args.oid:raise ValueError('created ref is missing')
            data['state']='created'
    fd,name=tempfile.mkstemp(prefix='reserved-tag-',suffix='.tmp',dir=path.parent)
    try:
        with os.fdopen(fd,'w') as output:
            json.dump(data,output,sort_keys=True)
            output.flush()
            os.fsync(output.fileno())
        os.replace(name,path)
    finally:
        Path(name).unlink(missing_ok=True)

if __name__=='__main__':
    try:main()
    except (ValueError,RuntimeError,OSError,KeyError,IndexError) as error:sys.exit('reserved-ref record: '+str(error))
