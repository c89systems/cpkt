#!/usr/bin/env python3
"""Observe selected package phase failures and interruption in real children."""
import os
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
    driver=root/'driver.py'
    driver.write_text('''import os,sys
sys.path.insert(0,sys.argv[1]+'/scripts')
from cpkt_package_command import run
for phase in ('configure','fixture','build','test','package'):
    run([sys.executable,sys.argv[2],phase],root=sys.argv[3],phase=phase,env=os.environ,pass_fds=())
''')
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
        args=[sys.executable,str(driver),str(source),str(stub),str(root)]
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
            assert 'phase='+phase in diagnostic and 'target='+target in diagnostic,output
            expected_status=128+signum if interruption else 143 if mode=='child' else status
            if not via_make:assert process.returncode==expected_status,(name,output,process.returncode)
            else:assert process.returncode!=0
            assert 'status='+str(expected_status) in diagnostic,output
            if interruption:assert 'received '+signal.Signals(signum).name in diagnostic and 'sender' in diagnostic,output
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
