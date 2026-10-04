#!/usr/bin/env python3
"""Authoritative Make operation dispatch with strict selectors before mutation."""
import argparse
import os
from pathlib import Path
import subprocess
import shlex
import sys

from cpkt_inventory import load, GROUPS, REPOSITORY_GROUP
from cpkt_operation import delegated, run as locked_run, child_delegation
from cpkt_presets import preset_info

ROOT=Path(__file__).resolve().parent.parent
COMPLETE={'prerelease','prerelease-hardening','release-pipeline','release-matrix','release-final-matrix','release','test-all','deps-release','deps-cross','cross-build','cross-test'}
SOURCE={'package-source','package-source-smoke','source-archive','verify-source-archive'}
NATIVE={'debug','test-debug','test-host','build-debug','build-host','clangd-surface','finalize-slice','valgrind','fuzz','fuzz-smoke','fuzz-long','e2e-postgres','test-e2e','examples'}


def command(args,group):
    with child_delegation(ROOT,group) as (env,fds):
        return subprocess.run(list(map(str,args)),cwd=ROOT,env=env,pass_fds=fds,check=True)


def backend(action,group,preset,*extra):
    command([sys.executable,ROOT/'scripts/group-build.py',action,'--group',group,'--preset',preset,*extra],group)


def perform(action,group,preset,explicit,scope,scope_explicit,dependency):
    def child(name,**overrides):
        perform(name,overrides.get('group',group),overrides.get('preset',preset),overrides.get('explicit',explicit),overrides.get('scope',scope),False,dependency)
    if action=='prerelease':child('release-pipeline')
    elif action=='prerelease-hardening':child('prerelease');child('fuzz',preset='debug',explicit=False)
    elif action in ('build','build-release','test','test-cross'):
        selected=[preset] if group!='all' or explicit else [t+'-release' for t in load(ROOT)['package_targets'] if '-linux-' in t]
        for item in selected:backend('test' if action.startswith('test') else 'build',group,item)
    elif action in ('build-debug','build-host'):backend('build',group,preset)
    elif action in ('debug','test-debug','test-host'):backend('test',group,preset)
    elif action in ('cross-build','cross-test'):child('test-cross' if action=='cross-test' else 'build-release',explicit=False)
    elif action in ('deps','deps-all','deps-debug'):
        if action=='deps':backend('deps',group,preset,'--dependency',dependency)
        else:backend('deps' if action=='deps-all' else 'configure',group,preset)
    elif action in ('deps-release','deps-cross'):
        for target in load(ROOT)['package_targets']:
            if action=='deps-cross' and target.startswith('x86_64'):continue
            if action=='deps-release' and target.endswith('darwin'):continue
            backend('configure',group,target+'-release')
    elif action=='examples':
        examples={item['group'] for name,item in load(ROOT)['tests'].items() if 'example' in name}
        selected=[REPOSITORY_GROUP] if group=='all' else [group]
        if any(item not in examples for item in selected):
            raise ValueError('no example tests for GROUP='+group)
        for item in selected:
            if group=='all' and item=='core' and any(other!='core' for other in selected):
                backend('test','core',preset)
            else:
                backend('test',item,preset,'--regex','example')
    elif action=='clangd-surface':
        backend('build',group,preset)
        _,target,configuration=preset_info(ROOT,preset)
        command(['bash',ROOT/'scripts/verify-clangd-surface.sh',ROOT,ROOT/'build'/target/REPOSITORY_GROUP/configuration,'--group',group],group)
    elif action=='finalize-slice':
        child('format');child('debug');child('clangd-surface');child('format-check')
    elif action=='valgrind':
        backend('memcheck',group,preset)
        if REPOSITORY_GROUP=='db':
            with child_delegation(ROOT,'db') as (env,fds):
                env['CPKT_POSTGRES_E2E_MEMCHECK']='1'
                subprocess.run(['bash',str(ROOT/'scripts/test-e2e.sh'),str(ROOT/'build/x86_64-linux-gnu/db/Valgrind/cpkt_postgres_integration_test')],env=env,pass_fds=fds,check=True)
    elif action in ('fuzz','fuzz-smoke','fuzz-long'):
        command(['bash',ROOT/'scripts/fuzz.sh',{'fuzz':'standard','fuzz-smoke':'smoke','fuzz-long':'long'}[action]],group)
    elif action in ('e2e-postgres','test-e2e'):
        backend('build','db',preset,'--target','cpkt_postgres_integration_test')
        _,target,configuration=preset_info(ROOT,preset)
        command(['bash',ROOT/'scripts/test-e2e.sh',ROOT/'build'/target/'db'/configuration/'cpkt_postgres_integration_test'],'db')
    elif action.startswith('dev-'):command(['bash',ROOT/'scripts/devenv.sh',action[4:]],'db')
    elif action in ('package','package-verify','package-checksums','test-install-tree','verify-release-archives','verify-release-privacy'):
        operation='package' if action=='package' else 'checksums' if action=='package-checksums' else 'verify'
        selected_scope='release' if action.startswith('verify-release-') else scope or ('binary' if group=='all' else 'selected')
        args=[sys.executable,ROOT/'scripts/cpkt_packages.py',operation,'--group',group,'--scope',selected_scope]
        if group!='all':args+=['--preset',preset]
        command(args,group)
    elif action=='package-source':command(['bash',ROOT/'scripts/package-source.sh'],'all')
    elif action in ('package-source-smoke','source-archive','verify-source-archive'):
        if action!='verify-source-archive':child('package-source')
        ver=subprocess.check_output(['bash',ROOT/'scripts/release-version.sh',ROOT],text=True).strip()
        command(['bash',ROOT/'scripts/source-archive-verify.sh',ROOT/'dist'/f'cpkt-{ver}.tar.gz',ver],'all')
    elif action in ('release-matrix','release-final-matrix'):
        if action=='release-final-matrix':os.environ['CPKT_RELEASE_PRODUCTION']='1'
        child('package',scope='binary',explicit=False)
        if action=='release-final-matrix':child('package-source-smoke',explicit=False)
        final_scope='release' if action=='release-final-matrix' else 'binary'
        child('package-checksums',scope=final_scope,explicit=False)
        child('package-verify',scope=final_scope,explicit=False)
    elif action in ('release-pipeline','release','test-all'):
        if action=='release':
            os.environ['CPKT_RELEASE_PRODUCTION']='1'
            child('lifecycle-version-contract');child('clean')
        if action!='test-all':child('format');child('format-check')
        backend('preflight','all','debug')
        child('debug',preset='debug',explicit=False)
        if REPOSITORY_GROUP=='db':child('e2e-postgres',preset='debug',explicit=False)
        child('clangd-surface',preset='debug',explicit=False);child('valgrind',preset='debug',explicit=False)
        if REPOSITORY_GROUP!='db':child('fuzz-smoke',preset='debug',explicit=False)
        if action!='test-all':child('release-final-matrix' if action=='release' else 'release-matrix',explicit=False)
        if action=='prerelease-hardening':child('fuzz',preset='debug',explicit=False)
    elif action=='prerelease-live':
        if os.environ.get('CPKT_LIVE_CHECKS') != '1':raise ValueError('prerelease-live requires CPKT_LIVE_CHECKS=1')
        child('e2e-sus',group='misc',preset='debug');child('e2e-cpktxscribe',group='misc',preset='debug')
    elif action=='deps-core':
        if REPOSITORY_GROUP=='core':raise ValueError('core has no external core prerequisite')
        _,target,_=preset_info(ROOT,preset)
        command([sys.executable,ROOT/'scripts/cpkt_core.py','ensure','--target',target],group)
    elif action=='lifecycle-version-contract':command([sys.executable,ROOT/'scripts/cpkt_reserved_tag.py','check'],'all')
    elif action in ('clean','clean-dist'):backend('clean',group,preset,*(['--dist-only'] if action=='clean-dist' else []))
    elif action in ('format','format-check'):
        files=[str(p) for directory in ('include','src','tests','examples','fuzz','tools') for p in (ROOT/directory).rglob('*') if p.suffix in ('.c','.h','.cpp','.cc','.cxx','.hpp') and p.is_file()]
        command(['clang-format',*(['-i'] if action=='format' else ['--dry-run','--Werror']),*sorted(files)],group)
    elif action.startswith('test-darwin-'):command([sys.executable,ROOT/'scripts/cpkt_darwin.py','source' if action=='test-darwin-native' else 'sdk'],'all')
    elif action=='test-github-actions-contracts':command([sys.executable,ROOT/'tests/github_actions_contract_test.py'],'all')
    elif action=='cpktxscribe':backend('build','misc',preset,'--target','cpktxscribe')
    elif action in ('e2e-sus','e2e-cpktxscribe'):
        target='cpkt_sus_audio_integration_test' if action=='e2e-sus' else 'cpktxscribe'
        backend('build','misc',preset,'--target',target)
        _,target_id,configuration=preset_info(ROOT,preset);directory=ROOT/'build'/target_id/'misc'/configuration
        command(['bash',ROOT/'scripts'/(action+'.sh'),directory/('tools/cpktxscribe' if target=='cpktxscribe' else target),directory],'misc')
    elif action.startswith('example-'):
        target='cpkt_'+action[8:].replace('-','_')+'_c89_example'
        if target.endswith('_static_c89_example'):
            target=target.replace('_static_c89_example','_static_c89_example')
        backend('build','misc',preset,'--target',target)
        _,target_id,configuration=preset_info(ROOT,preset);binary=ROOT/'build'/target_id/'misc'/configuration/target
        if action.endswith('-static'):print(binary)
        elif 'intro' in action:
            args=['bash',ROOT/'scripts'/('run-'+action[8:]+'.sh'),binary]
            if action.startswith('example-sus-'):args.append(binary.parent)
            command(args,'misc')
        else:command([binary,*shlex.split(os.environ.get('CPKT_SUS_LIVE_VOX_ARGS' if action.startswith('example-sus-') else 'CPKT_AUDIO_LIVE_VOX_ARGS',''))],'misc')
    else:raise ValueError('unsupported lifecycle action: '+action)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action');parser.add_argument('--group',default=os.environ.get('GROUP','all'))
    parser.add_argument('--preset',default=os.environ.get('PRESET','debug'));parser.add_argument('--preset-explicit',choices=['yes','no'],default='yes' if 'PRESET' in os.environ else 'no')
    parser.add_argument('--scope',default=os.environ.get('SCOPE',''));parser.add_argument('--scope-explicit',choices=['yes','no'],default='yes' if 'SCOPE' in os.environ else 'no');parser.add_argument('--dependency',default=os.environ.get('DEPENDENCY',''))
    args=parser.parse_args();explicit=args.preset_explicit=='yes';scope_explicit=args.scope_explicit=='yes'
    if not explicit and args.action.startswith('example-') and args.action.endswith('-static'):
        args.preset=os.environ.get('STATIC_LIVE_PRESET','x86_64-linux-musl-release')
    if not explicit and args.action in ('e2e-sus','e2e-cpktxscribe'):
        args.preset=os.environ.get('E2E_SUS_PRESET','debug')
    if args.group not in GROUPS:parser.error('unknown GROUP')
    if args.scope and args.scope not in ('selected','binary','release'):parser.error('unknown SCOPE')
    _,target,configuration=preset_info(ROOT,args.preset)
    if args.action in COMPLETE|SOURCE and (args.group!='all' or explicit):parser.error(args.action+' requires GROUP=all with no explicit PRESET narrowing')
    expected_scope='release' if args.action in ('release','release-final-matrix','verify-release-archives','verify-release-privacy') else 'binary' if args.action in COMPLETE else None
    if scope_explicit and expected_scope and args.scope!=expected_scope:parser.error(args.action+' requires SCOPE='+expected_scope)
    if args.action in NATIVE and (args.preset not in ('debug','arm64-apple-darwin-debug') or configuration!='Debug'):parser.error(args.action+' requires native Debug PRESET')
    if args.action in ('package','package-verify','package-checksums','test-install-tree'):
        if args.group!='all' and (not explicit or configuration!='Release'):parser.error('selected packaging requires explicit release PRESET')
        if args.group=='all' and explicit:parser.error('all packaging rejects PRESET narrowing')
        if args.scope and (args.scope=='selected')!=(args.group!='all'):parser.error('GROUP/SCOPE mismatch')
    if args.action=='deps' and not args.dependency:parser.error('deps requires DEPENDENCY=<name>')
    if args.action in ('e2e-postgres','test-e2e') or args.action.startswith('dev-'):
        if REPOSITORY_GROUP!='db':parser.error('database service operations require GROUP=db|all')
    if args.action.startswith('fuzz') and REPOSITORY_GROUP=='db':parser.error('GROUP=db fuzz is unsupported; aggregate db fuzz is N/A')
    if args.action.startswith('example-') or args.action in ('e2e-sus','e2e-cpktxscribe','cpktxscribe'):
        if REPOSITORY_GROUP!='misc':parser.error('audio/speech operations require GROUP=misc|all')
    if args.action in ('format','format-check') and args.scope:parser.error('formatting is global and does not accept SCOPE')
    if 'CPKT_OPERATION_FD' not in os.environ:return locked_run(ROOT,args.group,[sys.executable,__file__]+sys.argv[1:])
    delegated(ROOT,args.group)
    return perform(args.action,args.group,args.preset,explicit,args.scope,scope_explicit,args.dependency)


if __name__=='__main__':
    import shlex
    try:sys.exit(main())
    except (ValueError,RuntimeError,OSError,subprocess.CalledProcessError) as error:sys.exit('lifecycle: '+str(error))
