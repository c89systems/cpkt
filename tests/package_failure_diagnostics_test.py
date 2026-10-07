#!/usr/bin/env python3
"""Observe selected package phase failures and interruption in real children."""
import os
import shlex
import fcntl
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

if not __debug__:raise SystemExit('Package diagnostic tests require Python assertions enabled')
source=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='package diagnostics-',dir=source/'build') as temporary:
    root=Path(temporary)
    from native_lifecycle_fixture import seed
    seed(root)
    driver=root/'driver.sh'
    driver.write_text('#!/usr/bin/env bash\nset -euo pipefail\nfor phase in configure fixture build test package; do\n  bash "$1/scripts/package-command.sh" "$3" "$phase" -- '+shlex.quote(sys.executable)+' "$2" "$phase"\ndone\n')
    stub=root/'stub.py'
    stub.write_text('''import os,sys,time,signal
phase=sys.argv[1]
with open(os.environ['DIAG_CALLS'],'a') as stream:stream.write(phase+'\\n')
if phase==os.environ['DIAG_PHASE']:
    mode=os.environ['DIAG_MODE']
    if mode=='fail':sys.exit(int(os.environ['DIAG_STATUS']))
    if mode=='child':os.kill(os.getpid(),signal.SIGTERM)
    if mode=='parent':os.kill(os.getppid(),signal.SIGTERM)
    if mode=='group':
        open(os.environ['DIAG_READY'],'w').close();time.sleep(30)
''')
    phases=['configure','fixture','build','test','package'];cases=[]
    def run(name,phase='',mode='fail',status=23,signum=signal.SIGTERM,via_make=False,target='x86_64-linux-gnu-release'):
        calls=root/(name+'.calls');ready=root/(name+'.ready')
        env=dict(os.environ,DIAG_CALLS=str(calls),DIAG_READY=str(ready),DIAG_PHASE=phase,DIAG_MODE=mode,DIAG_STATUS=str(status),CPKT_PRESET=target)
        for key in list(env):
            if key.startswith('CPKT_OPERATION_') or key in ('MAKEFLAGS','MFLAGS','MAKELEVEL'):env.pop(key,None)
        args=['bash',str(source/'scripts/operation.sh'),'--root',str(root),'--group','all','--','bash',str(driver),str(source),str(stub),str(root)]
        if via_make:
            import shlex
            (root/'Makefile').write_text('all:\n\t'+shlex.join(args)+'\n');args=['make','--no-print-directory']
        process=subprocess.Popen(args,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,start_new_session=True)
        try:
            if mode=='group':
                deadline=time.monotonic()+10
                while not ready.exists():
                    if process.poll() is not None or time.monotonic()>deadline:raise AssertionError('phase never started')
                    time.sleep(.01)
                os.killpg(process.pid,signum)
            output,_=process.communicate(timeout=15)
        except BaseException:
            try:os.killpg(process.pid,signal.SIGKILL)
            except ProcessLookupError:pass
            process.communicate();raise
        actual=calls.read_text().splitlines();expected=phases[:phases.index(phase)+1] if phase else phases
        assert actual==expected,(name,actual,output)
        if not phase:assert process.returncode==0,output
        else:
            interruption=mode in ('group','parent')
            prefix='[package] '+('INTERRUPTED' if interruption else 'FAILED')
            diagnostics=[line for line in output.splitlines() if line.startswith(prefix)]
            assert len(diagnostics)==1,(name,output)
            diagnostic=diagnostics[0]
            assert 'phase='+phase in diagnostic and 'target='+target in diagnostic,(name,output)
            expected_status=128+signum if interruption else 143 if mode=='child' else status
            if not via_make:assert process.returncode==expected_status,(name,output,process.returncode)
            else:assert process.returncode!=0
            assert 'status='+str(expected_status) in diagnostic,(name,output)
            if interruption:assert 'received '+signal.Signals(signum).name in diagnostic and 'sender' in diagnostic,(name,output)
            else:
                assert 'command=' in diagnostic and 'received SIGTERM' not in output,output
                if expected_status==143:assert 'SIGTERM' in output and 'explicit exit' in output,output
        cases.append(name)
    for phase in phases:run('failure-'+phase,phase)
    run('darwin-prototype-fixture','fixture',target='arm64-apple-darwin-release')
    run('explicit-143','test',status=143)
    run('child-term','test',mode='child')
    run('parent-term','test',mode='parent')
    for signum in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP):run(signal.Signals(signum).name,'test',mode='group',signum=signum)
    run('make-group-term','test',mode='group',via_make=True)
    run('success')
    assert len(cases)==14
    print('Package phase/status/sender/signal/fail-fast diagnostics passed: '+', '.join(cases))

# Observe descendant ownership through an advisory lock instead of a host-wide
# process search. The leaf ignores TERM and can retain both captured pipes.
with tempfile.TemporaryDirectory(prefix='package cancellation-',dir=source/'build') as temporary:
    root=Path(temporary)
    leaf=root/'leaf.py';launcher=root/'launcher.py';driver=root/'driver.py'
    leaf.write_text("""import fcntl,os,signal,time
from pathlib import Path
for signum in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP):signal.signal(signum,signal.SIG_IGN)
def cleanup_started(signum,frame):
    Path(os.environ['TREE_CLEANUP']).touch()
signal.signal(signal.SIGTERM,cleanup_started)
with open(os.environ['TREE_LEASE'],'w') as lease:
    fcntl.flock(lease,fcntl.LOCK_EX)
    Path(os.environ['TREE_READY']).write_text(str(os.getpid()))
    time.sleep(4)
    Path(os.environ['TREE_LATE']).write_text('orphan changed outputs')
    time.sleep(30)
""")
    launcher.write_text("""import os,subprocess,sys,time
from pathlib import Path
level=int(sys.argv[1])
if level:
    os.execvp('bash',['bash',os.environ['TREE_SOURCE']+'/scripts/package-command.sh',os.environ['TREE_ROOT'],'build','--',sys.executable,__file__,str(level-1)])
else:
    subprocess.Popen([sys.executable,os.environ['TREE_LEAF']])
    while not Path(os.environ['TREE_READY']).exists():time.sleep(.01)
    if os.environ['TREE_EXIT_LEADER']=='1':sys.exit(0)
    if os.environ['TREE_EXIT_LEADER']=='fail':sys.exit(23)
    time.sleep(30)
""")
    driver.write_text("""import os,sys
os.execvp('bash',['bash',os.environ['TREE_SOURCE']+'/scripts/package-command.sh',os.environ['TREE_ROOT'],'build','--',sys.executable,os.environ['TREE_LAUNCHER'],os.environ['TREE_DEPTH']])
""")
    cases=[]
    for capture,depth,exit_leader in ((False,0,False),(True,0,True),(True,2,False)):
        for signum in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP):
            name=str(int(capture))+'-'+str(depth)+'-'+str(int(exit_leader))+'-'+signal.Signals(signum).name
            ready=root/(name+'.ready');lease=root/(name+'.lease');late=root/(name+'.late');cleanup=root/(name+'.cleanup')
            env=dict(os.environ,TREE_SOURCE=str(source),TREE_ROOT=str(root),TREE_LEAF=str(leaf),TREE_LAUNCHER=str(launcher),TREE_DEPTH=str(depth),TREE_CAPTURE=str(int(capture)),TREE_EXIT_LEADER=str(int(exit_leader)),TREE_READY=str(ready),TREE_LEASE=str(lease),TREE_LATE=str(late),TREE_CLEANUP=str(cleanup),_CPKT_PACKAGE_TERMINATION_GRACE_SECONDS='.5')
            for key in list(env):
                if key.startswith('CPKT_OPERATION_') or key in ('MAKEFLAGS','MFLAGS','MAKELEVEL'):env.pop(key,None)
            process=subprocess.Popen([sys.executable,str(driver)],cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,start_new_session=True)
            try:
                deadline=time.monotonic()+5
                while not ready.exists():
                    assert process.poll() is None and time.monotonic()<deadline,'descendant never acquired its lease'
                    time.sleep(.01)
                if exit_leader:
                    while not cleanup.exists():
                        assert process.poll() is None and time.monotonic()<deadline,'cleanup never started'
                        time.sleep(.01)
                started=time.monotonic()
                os.kill(process.pid,signum)
                output,_=process.communicate(timeout=3)
                assert process.returncode==128+signum,(name,process.returncode,output)
                assert time.monotonic()-started<3,(name,output)
                with lease.open('r+') as probe:
                    fcntl.flock(probe,fcntl.LOCK_EX|fcntl.LOCK_NB)
                assert not late.exists(),(name,'descendant modified outputs',output)
            except BaseException:
                # Kill only groups created by this fixture. A negative run of
                # the old helper inherits the fixture's original group.
                if ready.exists():
                    try:os.killpg(os.getpgid(int(ready.read_text())),signal.SIGKILL)
                    except ProcessLookupError:pass
                try:os.killpg(process.pid,signal.SIGKILL)
                except ProcessLookupError:pass
                process.communicate(timeout=5)
                raise
            cases.append(name)
    for capture in (False,True):
        name='failed-leader-'+str(int(capture))
        ready=root/(name+'.ready');lease=root/(name+'.lease');late=root/(name+'.late')
        env=dict(os.environ,TREE_SOURCE=str(source),TREE_ROOT=str(root),TREE_LEAF=str(leaf),TREE_LAUNCHER=str(launcher),TREE_DEPTH='0',TREE_CAPTURE=str(int(capture)),TREE_EXIT_LEADER='fail',TREE_READY=str(ready),TREE_LEASE=str(lease),TREE_LATE=str(late),TREE_CLEANUP=str(root/(name+'.cleanup')),_CPKT_PACKAGE_TERMINATION_GRACE_SECONDS='.5')
        for key in list(env):
            if key.startswith('CPKT_OPERATION_') or key in ('MAKEFLAGS','MFLAGS','MAKELEVEL'):env.pop(key,None)
        process=subprocess.Popen([sys.executable,str(driver)],cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,start_new_session=True)
        try:
            output,_=process.communicate(timeout=3)
            assert process.returncode==23,(name,output)
            assert '[package] FAILED' in output and 'status=23' in output,(name,output)
            assert ready.exists(),(name,'leaf did not start')
            with lease.open('r+') as probe:fcntl.flock(probe,fcntl.LOCK_EX|fcntl.LOCK_NB)
            assert not late.exists(),(name,'failed command retained a writer')
        except BaseException:
            if ready.exists():
                try:os.killpg(os.getpgid(int(ready.read_text())),signal.SIGKILL)
                except ProcessLookupError:pass
            try:os.killpg(process.pid,signal.SIGKILL)
            except ProcessLookupError:pass
            process.communicate(timeout=5)
            raise
        cases.append(name)
    assert len(cases)==11
    print('Cancellation reaps owned descendants and closes captured pipes: '+', '.join(cases))
