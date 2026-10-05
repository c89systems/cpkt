#!/usr/bin/env python3
"""Compile and execute an installed-consumer loader probe before SDK builds."""
import argparse
import json
import os
from pathlib import Path
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from cpkt_inventory import REPOSITORY_GROUP
from cpkt_operation import delegated,run
from cpkt_sdk_consumer import command,execute
from cpkt_packages import safe_owned

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('scratch','target','compiler','readelf','sysroot'):
        parser.add_argument('--'+name,required=True)
    args=parser.parse_args()
    if 'CPKT_OPERATION_FD' not in os.environ:
        return run(ROOT,REPOSITORY_GROUP,[sys.executable,__file__,*sys.argv[1:]])
    delegated(ROOT,REPOSITORY_GROUP)
    scratch=safe_owned(Path(args.scratch))
    if not scratch.is_relative_to(ROOT/'build'):
        raise ValueError('loader probe scratch must be under repository build/')
    scratch.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sdk-loader-',dir=scratch) as work:
        prefix=Path(work).resolve();(prefix/'lib').mkdir()
        source=prefix/'probe.c';binary=prefix/'probe'
        source.write_text('#include <stdio.h>\nint main(void) { return puts("cpkt-loader-probe") < 0; }\n')
        command(['bash',ROOT/'scripts/run-no-warnings.sh','SDK loader probe compile',args.compiler,
            '-std=c89','-pedantic-errors','-Wall','-Wextra','-Werror',source,'-o',binary],group=REPOSITORY_GROUP)
        configured={'CMAKE_READELF':args.readelf,'CMAKE_SYSROOT':args.sysroot,
                    'CPKT_INSTALLED_PREFIX':str(prefix)}
        result=execute(binary,[],args.target,configured)
        if result['status']!='passed' or not result['loader_resolution']:
            raise ValueError('dynamic SDK loader probe did not exercise runtime resolution')
        print(json.dumps(result,sort_keys=True))
    return 0

if __name__=='__main__':
    try:sys.exit(main())
    except (ValueError,OSError,RuntimeError) as error:sys.exit('SDK loader preflight: '+str(error))
