"""Package command failure/signal context, shared by producers and consumers."""
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import time

class Interrupted(Exception):
    def __init__(self,signum):self.signum=signum


def run(args, *, root, phase, env, pass_fds, capture=False, cwd=None):
    started=time.monotonic();process=None;handlers={};received=None
    def interrupted(signum,frame):
        nonlocal received
        if received is None:
            received=signum
            raise Interrupted(signum)
    try:
        for signum in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP):
            handlers[signum]=signal.signal(signum,interrupted)
        process=subprocess.Popen(list(map(str,args)),cwd=cwd or root,env=env,pass_fds=pass_fds,
                                 text=True,stdout=subprocess.PIPE if capture else None,
                                 stderr=subprocess.PIPE if capture else None)
        output,error=process.communicate()
        status=process.returncode
        if status<0:status=128-status
        if status:
            if capture:print((output or '')+(error or ''),file=sys.stderr)
            diagnose(root,phase,status,started,args)
            raise SystemExit(status)
        return output or ''
    except Interrupted as event:
        # Stop the active child and reap it before releasing the operation lock.
        for signum in handlers:signal.signal(signum,signal.SIG_IGN)
        if process is not None:
            if process.poll() is None:process.terminate()
            try:process.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill();process.communicate()
        status=128+event.signum
        diagnose(root,phase,status,started,args,event.signum)
        raise SystemExit(status)
    finally:
        for signum,handler in handlers.items():signal.signal(signum,handler)


def diagnose(root,phase,status,started,args,signum=None):
    control=Path(root)/'build/control';control.mkdir(parents=True,exist_ok=True)
    run=os.environ.get('CPKT_OPERATION_RUN')
    if run:
        marker=control/('package-diagnostic-'+run)
        try:fd=os.open(marker,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
        except FileExistsError:return
        os.close(fd)
    preset=os.environ.get('CPKT_PRESET',os.environ.get('PRESET','unknown'))
    message='[package] '+('INTERRUPTED' if signum else 'FAILED')+' target='+preset+' phase='+phase+' status='+str(status)+' pid='+str(os.getpid())+' elapsed='+str(round(time.monotonic()-started,3))+'s'
    if signum:message+=' received '+signal.Signals(signum).name+'; sender identity is unavailable'
    else:message+=' command='+shlex.join(list(map(str,args)))
    print(message,file=sys.stderr,flush=True)
    if not signum and status>128:
        try:name=signal.Signals(status-128).name
        except ValueError:name='an unknown signal'
        print('[package] Status '+str(status)+' can represent '+name+' or an explicit exit('+str(status)+'); exit status alone does not identify a signal sender.',file=sys.stderr)
