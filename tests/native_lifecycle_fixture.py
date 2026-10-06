"""Isolated public-command fixtures; never compile repository dependencies."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def environment():
    return {key:value for key,value in os.environ.items()
            if not key.startswith('CPKT_OPERATION_') and key not in ('GROUP','PRESET','SCOPE','CPKT_PRESET','CPKT_RESOLVED_TARGET','CPKT_CONFIGURED_BINARY_DIR','CPKT_CONFIGURED_GROUP')}

def seed(root):
    root=Path(root)
    root.mkdir(parents=True,exist_ok=True)
    if not (root/'VERSION').exists():(root/'VERSION').write_text('1.2.3\n')
    for directory in ('scripts','cmake'):
        (root/directory).mkdir(parents=True,exist_ok=True)
    for source in (ROOT/'scripts').glob('*'):
        if source.is_file() and source.suffix in ('.sh','.py'):
            shutil.copy2(source,root/'scripts'/source.name)
    for source in (ROOT/'cmake').glob('*.cmake*'):
        shutil.copy2(source,root/'cmake'/source.name)
    shutil.copy2(ROOT/'cmake/components.json',root/'cmake/components.json')
    shutil.copy2(ROOT/'CMakePresets.json',root/'CMakePresets.json')

def git(root,*arguments,check=True):
    result=subprocess.run(['git','-C',str(root),*arguments],capture_output=True,text=True)
    if check and result.returncode:raise RuntimeError(result.stderr)
    return result.stdout.strip() if result.returncode==0 else None

def reserved(root,action,*,interrupt=False,foreign=False):
    root=Path(root);seed(root)
    if git(root,'rev-parse','--show-toplevel')!=str(root.resolve()):
        raise RuntimeError('reserved-ref fixture requires its own throwaway Git repository')
    (root/'Makefile').write_text('print-release-version:\n\t@bash scripts/release-version.sh "$(CURDIR)"\n')
    variables=environment()
    if (root/'build').is_symlink():
        raise RuntimeError('fixture refuses a symlinked generated-state root')
    if interrupt or foreign:
        tools=root/'build/test-tools';tools.mkdir(parents=True,exist_ok=True)
        real=shlex.quote(shutil.which('git'))
        if foreign:
            hook='''if [ "${1:-}" = update-ref ] && [ "${2:-}" = --create-reflog ]; then
  shift 2
  while [ "${1:-}" != refs/tags/v99.99.99 ]; do shift; done
  reference=$1; oid=$2; zeros=$3
  REAL update-ref --create-reflog -m 'foreign exclusive winner' "$reference" "$oid" "$zeros"
  exit 42
fi
'''
        else:
            hook='''if [ "${1:-}" = update-ref ] && [ "${2:-}" = --create-reflog ]; then
  REAL "$@"
  kill -KILL "$PPID"
  exit 0
fi
'''
        wrapper=tools/'git'
        wrapper.write_text('#!/usr/bin/env bash\nset -euo pipefail\noriginal=("$@")\nif [ "${1:-}" = -C ]; then shift 2; fi\n'+hook.replace('REAL',real)+'exec '+real+' "${original[@]}"\n')
        wrapper.chmod(0o755)
        variables['PATH']=str(tools)+os.pathsep+variables['PATH']
    result=subprocess.run(['bash',str(root/'scripts/version-contract.sh'),action],cwd=root,
                          env=variables,capture_output=True,text=True)
    if interrupt:
        if result.returncode==0 or git(root,'show-ref','--verify','--hash','refs/tags/v99.99.99',check=False) is None:
            raise RuntimeError('fixture failed to interrupt owned ref creation: '+result.stdout+result.stderr)
    elif result.returncode:
        raise RuntimeError(result.stdout+result.stderr)
    return result

def create(root):
    reserved(root,'check',interrupt=True)

def recover(root):
    reserved(root,'recover')


def capture(root):
    """Record public Bash workflow commands with inert native-tool stand-ins."""
    root=Path(root);seed(root)
    tools=root/'build/test-tools';tools.mkdir(parents=True,exist_ok=True)
    record=root/'build/calls.jsonl'
    cmake=tools/'cmake'
    cmake.write_text('#!'+shutil.which('python3')+'\n'+
        'import json,os,subprocess,sys\nfrom pathlib import Path\n'+
        'a=sys.argv[1:]\n'+
        'if "--list-presets=configure" in a or "-P" in a: raise SystemExit(subprocess.call(['+repr(shutil.which('cmake'))+',*a]))\n'+
        'with open('+repr(str(record))+',"a") as f:f.write(json.dumps(dict(args=a,sdk=os.environ.get("SDKROOT"),production=os.environ.get("CPKT_RELEASE_PRODUCTION")))+"\\n")\n')
    cmake.chmod(0o755)
    (root/'scripts/cpkt_build_evidence.py').write_text('# inert structured-evidence fixture\n')
    (root/'scripts/cpkt-toolchains.sh').write_text('#!/bin/sh\nprintf "status=ready\\n"\n')
    variables=environment();variables['CMAKE']=str(cmake)
    return variables,record

def trace(root):
    """Record lifecycle leaves while exercising actual Bash routing and release."""
    root=Path(root);seed(root)
    record=root/'build/trace.jsonl';record.parent.mkdir(exist_ok=True)
    body='#!'+shutil.which('python3')+'\nimport json,os,sys\nwith open('+repr(str(record))+',"a") as f:f.write(json.dumps(dict(command=sys.argv,args=sys.argv[1:],production=os.environ.get("CPKT_RELEASE_PRODUCTION")))+"\\n")\n'
    for name in ('build.sh','format.sh','clean.sh','package.sh','source-reconstruct.sh','version-contract.sh','verify-clangd-surface.sh','memcheck.sh','fuzz.sh','e2e-postgres.sh','test-e2e.sh','package-source.sh','source-archive-verify.sh'):
        (root/'scripts'/name).write_text('#!/usr/bin/env bash\nexec python3 '+shlex.quote(str(root/'build/record.py'))+' '+shlex.quote(name)+' "$@"\n')
    (root/'build/record.py').write_text(body)
    (root/'VERSION').write_text('1.2.3\n')
    return environment(),record


def archive(prefix,destination):
    subprocess.run(['bash',str(ROOT/'scripts/archive.sh'),str(prefix),str(destination)],check=True,capture_output=True)


def metadata(root,configured=None):
    root=Path(root);seed(root)
    for name in ('LICENSE','docs/sdk-installation.md','cmake/payload-ownership.json'):
        destination=root/name;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,destination)
    data={'repository_group':'core','groups':{'core':{'requires':[],'package':{'docs':[],'examples':[],'files':[],'extra_notices':[]}}},'components':{},'targets':{},'tests':{}}
    (root/'cmake/components.json').write_text(json.dumps(data))
    (root/'cmake/CpktDependencies.cmake').write_text('# no synthetic upstream builds\n')
    producer=root/'build/x86_64-linux-gnu/core/producer';producer.mkdir(parents=True)
    (producer/'CMakeCache.txt').write_text('CPKT_CMOCKA_VERSION:STRING=1.1.7\n')
    values={'CMAKE_BUILD_TYPE':'Release','CPKT_TARGET_ID':'x86_64-linux-gnu','CPKT_GROUP':'core','CPKT_CMOCKA_VERSION':'1.1.7','CPKT_BUNDLE_VERSION':'1.2.3'}
    values.update(configured or {})
    mqtt=root/'.cache/deps/x86_64-linux-gnu/mqtt-c/install/share/cpkt/mqtt-c';mqtt.mkdir(parents=True)
    (mqtt/'README').write_text('synthetic public MQTT headers notice')
    source='cmake_minimum_required(VERSION 3.21)\nproject(metadata NONE)\n'
    for key,value in values.items():source+='set('+key+' [==['+str(value)+']==] CACHE STRING "")\n'
    source+='include(cmake/CpktGroups.cmake)\ninclude(cmake/CpktSDKInstall.cmake)\ncpkt_register_sdk_install("")\n'
    (root/'CMakeLists.txt').write_text(source)
    graph=root/'build/x86_64-linux-gnu/core/Release'
    variables=environment()
    result=subprocess.run(['cmake','-S',str(root),'-B',str(graph)],env=variables,capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    prefix=root/'build/installed SDK'
    result=subprocess.run(['bash',str(root/'scripts/operation.sh'),'--group','core','--','cmake','--install',str(graph),'--prefix',str(prefix)],env=variables,capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    return prefix,graph
