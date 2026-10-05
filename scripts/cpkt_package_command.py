"""Package command failure/signal context, shared by producers and consumers."""
import os
import math
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
    # Nested package helpers must finish their escalation before their caller's
    # grace period expires. This is private child-process coordination.
    grace=float(env.get('_CPKT_PACKAGE_TERMINATION_GRACE_SECONDS','10'))
    if not math.isfinite(grace) or not 0 < grace <= 10:
        raise ValueError('invalid package termination grace period')
    child_env=dict(env,_CPKT_PACKAGE_TERMINATION_GRACE_SECONDS=str(grace/2))
    def signal_group(signum):
        if process is not None:
            try:os.killpg(process.pid,signum)
            except ProcessLookupError:pass
    def stop_group():
        signal_group(signal.SIGTERM)
        try:process.communicate(timeout=grace)
        except subprocess.TimeoutExpired:pass
        # The leader can exit before grandchildren. Escalate the whole owned
        # group even when poll()/communicate() has already reaped that leader.
        signal_group(signal.SIGKILL)
        try:return process.communicate(timeout=grace)
        except subprocess.TimeoutExpired:
            # A deliberately detached process may retain a pipe; never let its
            # descriptor keep this failed phase or the operation lock alive.
            for stream in (process.stdout,process.stderr):
                if stream is not None:stream.close()
            process.wait(timeout=grace)
            return None,None
    def interrupted(signum,frame):
        nonlocal received
        if received is None:
            received=signum
            # Defer a signal during Popen until the child handle is available.
            if process is not None:raise Interrupted(signum)
    try:
        for signum in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP):
            handlers[signum]=signal.signal(signum,interrupted)
        if received is not None:raise Interrupted(received)
        process=subprocess.Popen(list(map(str,args)),cwd=cwd or root,env=child_env,pass_fds=pass_fds,
                                 start_new_session=True,
                                 text=True,stdout=subprocess.PIPE if capture else None,
                                 stderr=subprocess.PIPE if capture else None)
        if received is not None:raise Interrupted(received)
        if capture:
            while True:
                try:
                    output,error=process.communicate(timeout=.25)
                    break
                except subprocess.TimeoutExpired:
                    # A failed leader cannot delegate continued work to a leaf
                    # that retains stdout/stderr. Observe the exit while draining.
                    if process.poll() not in (None,0):
                        output,error=stop_group()
                        break
        else:output,error=process.communicate()
        status=process.returncode
        if status<0:status=128-status
        if status:
            stop_group()
            if capture:print((output or '')+(error or ''),file=sys.stderr)
            diagnose(root,phase,status,started,args)
            raise SystemExit(status)
        return output or ''
    except Interrupted as event:
        # Stop the active child and reap it before releasing the operation lock.
        for signum in handlers:signal.signal(signum,signal.SIG_IGN)
        if process is not None:stop_group()
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
