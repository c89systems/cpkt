#!/usr/bin/env python3
"""Independent native GNU Release reconstruction of all group products once."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from cpkt_operation import operation_fds, delegated
from cpkt_presets import preset_info
from cpkt_inventory import REPOSITORY_GROUP
from cpkt_receipts import cache

ROOT=Path(__file__).resolve().parent.parent


def reconstruction_environment(root,environment):
    selected=environment.get('CPKT_PRESET',environment.get('PRESET','debug'))
    item,target,mode=preset_info(root,selected)
    if target!='x86_64-linux-gnu':
        selected='x86_64-linux-gnu-release';item,target,mode=preset_info(root,selected)
    path=root/'build'/target/REPOSITORY_GROUP/mode/'CMakeCache.txt'
    configured=cache(path) if path.is_file() else {}
    limit=configured.get('CPKT_DEPENDENCY_BUILD_JOBS','8')
    if not re.fullmatch('[1-9][0-9]*',limit) or int(limit)>8:raise ValueError('invalid configured source reconstruction job limit')
    jobs=environment.get('CPKT_DEPENDENCY_BUILD_JOBS',environment.get('CMAKE_BUILD_PARALLEL_LEVEL',limit))
    if not re.fullmatch('[1-9][0-9]*',jobs) or int(jobs)>int(limit):raise ValueError('source reconstruction jobs must be positive and not exceed configured limit '+limit)
    shared=environment.get('CPKT_DEPENDENCY_CACHE') or configured.get('CPKT_DEPENDENCY_CACHE') or str(Path(environment.get('XDG_CACHE_HOME',str(Path.home()/'.cache')))/'cpkt/deps')
    generator=environment.get('CPKT_SOURCE_GENERATOR') if environment.get('CPKT_SOURCE_RECONSTRUCTION')=='1' else None
    generator=generator or configured.get('CMAKE_GENERATOR',item.get('generator','Ninja'))
    if generator not in ('Ninja','Unix Makefiles'):raise ValueError('unsupported source reconstruction generator')
    return {'CPKT_DEPENDENCY_BUILD_JOBS':jobs,'CMAKE_BUILD_PARALLEL_LEVEL':jobs,'CPKT_DEPENDENCY_CACHE':shared,'CPKT_SOURCE_GENERATOR':generator,'CPKT_SOURCE_RECONSTRUCTION':'1','CPKT_PRESET':'x86_64-linux-gnu-release'}


def checked(args):
    result=subprocess.run(args,pass_fds=operation_fds())
    if result.returncode:sys.exit(128-result.returncode if result.returncode<0 else result.returncode)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--environment-from',type=Path)
    args=parser.parse_args()
    if args.environment_from:
        print(json.dumps(reconstruction_environment(args.environment_from,os.environ)));return
    delegated(ROOT,'all')
    if any((ROOT/path).exists() for path in ('.cache','build/x86_64-linux-gnu')):
        sys.exit('source reconstruction requires fresh local compiled/install state')
    os.environ.update(reconstruction_environment(ROOT,os.environ))
    for action in ('preflight','test'):
        checked([sys.executable,str(ROOT/'scripts/group-build.py'),action,'--group','all','--preset','x86_64-linux-gnu-release'])
    for group in (REPOSITORY_GROUP,):
        checked([sys.executable,str(ROOT/'scripts/cpkt_packages.py'),'stage','--group',group,'--preset','x86_64-linux-gnu-release'])
    from cpkt_packages import selected_archive, version
    ver=version();base=ROOT/'build/source-composition';base.mkdir()
    for group in (REPOSITORY_GROUP,):
        source=selected_archive(ver,'x86_64-linux-gnu',group);shutil.copy2(source,base/source.name)
    checked([sys.executable,str(ROOT/'scripts/cpkt_packages.py'),'compose','--group','all','--preset','x86_64-linux-gnu-release','--base',str(base)])


if __name__=='__main__':
    try:main()
    except (ValueError,RuntimeError,OSError) as error:sys.exit('source reconstruction: '+str(error))
