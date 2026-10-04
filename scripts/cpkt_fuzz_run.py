#!/usr/bin/env python3
"""Run the selected AFL harness using read-only pinned tool discovery."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from cpkt_operation import operation_fds, delegated, run

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--group',choices=('core','misc'),required=True)
parser.add_argument('mode',choices=('smoke','standard','long'))
parser.add_argument('target',type=Path)
parser.add_argument('seeds',type=Path)
args = parser.parse_args()
try:
    if args.mode=='long' and os.environ.get('CPKT_FUZZ_LONG_ENABLE')!='1':raise RuntimeError('long fuzz requires CPKT_FUZZ_LONG_ENABLE=1')
    if 'CPKT_OPERATION_FD' not in os.environ:
        sys.exit(run(root,args.group,[sys.executable,__file__,*sys.argv[1:]]))
    fd, _ = delegated(root,args.group)
    subprocess.run(['bash',str(root/'scripts/require-native-hardening-host.sh'),'afl++'],check=True)
    description=subprocess.check_output([sys.executable,str(root/'scripts/cpkt_afl_discover.py')],text=True)
    tools=dict(line.split('=',1) for line in description.splitlines() if '=' in line)
    duration={'smoke':'2','standard':'30','long':os.environ.get('CPKT_AFLPP_LONG_DURATION_SECONDS','300')}[args.mode]
    if not duration.isdecimal() or int(duration)<1:raise RuntimeError('AFL duration must be a positive integer')
    if not os.access(args.target,os.X_OK) or not args.seeds.is_dir():raise RuntimeError('selected AFL harness/seeds are absent')
    output=Path(tempfile.mkdtemp(prefix=args.target.name+'.afl-output.',dir=args.target.parent))
    env=dict(os.environ,AFL_PATH=tools['helper'],CPKT_AFLPP_ROOT=tools['root'],
             AFL_SKIP_CPUFREQ='1',AFL_NO_AFFINITY='1',AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES='1')
    status=subprocess.call([tools['afl_fuzz'],'-V',duration,'-i',str(args.seeds),'-o',str(output),'--',str(args.target),'@@'],env=env,pass_fds=operation_fds())
    findings=list(output.glob('**/crashes/id:*'))+list(output.glob('**/hangs/id:*'))
    if status or findings:
        raise RuntimeError('AFL failed or recorded crash/hang findings; retained '+str(output))
    shutil.rmtree(output)
except (RuntimeError,OSError,subprocess.CalledProcessError) as error:
    sys.exit(str(error))
