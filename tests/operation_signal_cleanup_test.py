#!/usr/bin/env python3
"""Repeated cancellation must not abandon an owned command during cleanup."""
import fcntl
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

from native_lifecycle_fixture import seed, environment

source = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='operation signals-', dir=source/'build') as directory:
    root = Path(directory)
    seed(root)
    leaf = root/'build/leaf.py'
    leaf.parent.mkdir(exist_ok=True)
    leaf.write_text('''import fcntl,os,signal,time
from pathlib import Path
def interrupted(signum,frame):
    Path(os.environ['SIGNAL_CLEANUP']).touch()
for signum in (signal.SIGHUP,signal.SIGINT,signal.SIGTERM):
    signal.signal(signum,interrupted)
with open(os.environ['SIGNAL_LEASE'],'w') as lease:
    fcntl.flock(lease,fcntl.LOCK_EX)
    Path(os.environ['SIGNAL_READY']).write_text(str(os.getpid()))
    time.sleep(30)
''')
    for signum in (signal.SIGHUP,signal.SIGINT,signal.SIGTERM):
        name = signal.Signals(signum).name
        ready = root/'build'/('ready-'+name)
        cleanup = root/'build'/('cleanup-'+name)
        lease = root/'build'/('lease-'+name)
        variables = environment()
        variables.update(SIGNAL_READY=str(ready), SIGNAL_CLEANUP=str(cleanup),
                         SIGNAL_LEASE=str(lease), CPKT_OPERATION_DEPTH='1')
        process = subprocess.Popen(['bash', str(source/'scripts/operation.sh'),
            '--root', str(root), '--group', 'all', '--', sys.executable, str(leaf)],
            env=variables, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, start_new_session=True)
        try:
            deadline = time.monotonic()+5
            while not ready.exists():
                assert process.poll() is None and time.monotonic()<deadline, 'command never started'
                time.sleep(.01)
            os.kill(process.pid,signum)
            while not cleanup.exists():
                assert process.poll() is None and time.monotonic()<deadline, 'cleanup never started'
                time.sleep(.01)
            os.kill(process.pid,signum)
            output,_ = process.communicate(timeout=4)
            assert process.returncode==128+signum,(name,process.returncode,output)
            with lease.open('r+') as probe:
                fcntl.flock(probe,fcntl.LOCK_EX|fcntl.LOCK_NB)
        finally:
            if ready.exists():
                try: os.killpg(int(ready.read_text()),signal.SIGKILL)
                except ProcessLookupError: pass
            try: os.killpg(process.pid,signal.SIGKILL)
            except ProcessLookupError: pass
            process.communicate(timeout=5)
        print(name+': repeated cancellation reaped the owned command',flush=True)
