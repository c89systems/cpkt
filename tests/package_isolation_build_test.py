#!/usr/bin/env python3
"""Fast behavioral lock/inventory/receipt/cleanup regressions, no SDK matrix."""
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts'))
from cpkt_inventory import load, components_for, record
from cpkt_receipts import publish, read, tree_identity, file_identity, validate_core, component_input_id, group_outputs
from cpkt_lock import delegated
from native_lifecycle_fixture import seed, environment


class Isolation(unittest.TestCase):
    def setUp(self):
        self.environment=environment()
        (ROOT / 'build').mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='isolation-regression-', dir=ROOT / 'build')
        self.root = Path(self.temp.name)
        seed(self.root)

    def tearDown(self):
        self.temp.cleanup()

    def test_linux_only_runtime_inventory_does_not_require_darwin_cases(self):
        source=(ROOT/'CMakeLists.txt').read_text()
        start=re.search(r'(?m)^[ \t]*if\(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin"\)',source).start()
        lines=source[start:].splitlines(keepends=True)
        depth=0
        end=start
        for line in lines:
            stripped=line.strip()
            if re.match(r'^if\s*\(',stripped,re.I):depth+=1
            elif re.match(r'^endif\s*\(',stripped,re.I):depth-=1
            end+=len(line)
            if depth==0:break
        self.assertEqual(0,depth)
        block=source[start:end]
        templates=[match.group(1).strip('"') for match in
            re.finditer(r'cpkt_group_add_test\s*\(\s*NAME\s+([^\s\)]+)',block)]
        inventory=json.loads((ROOT/'cmake/components.json').read_text())['tests']
        for template in templates:
            pattern=re.compile('^'+re.sub(r'\\\$\\\{[^}]+\\\}',r'.*',re.escape(template))+'$')
            matching=[registration for item in inventory.values()
                for registration in item.get('registrations',[])
                if pattern.fullmatch(registration['name'])
                and not any('CPKT_FACADE_ONLY' in condition for condition in registration['conditions'])]
            self.assertTrue(matching,template+' is absent from the test inventory')
            for registration in matching:
                self.assertIn('CMAKE_SYSTEM_NAME STREQUAL "Linux"',registration['conditions'],
                    registration['name']+' is required on Darwin despite Linux-only registration')

    def command(self, group, *arguments, timeout='0.3', env=None, cwd=None):
        arguments=list(arguments)
        if len(arguments)>1 and str(arguments[1]).endswith('operation.sh'):arguments[0]='bash'
        return subprocess.run(['bash', str(self.root / 'scripts/operation.sh'),
            '--root', str(self.root), '--group', group, '--timeout', timeout, '--', *arguments],
            capture_output=True, text=True, env=env if env is not None else self.environment, cwd=cwd)

    def test_service_execution_closes_reissued_descriptors_and_clears_ownership(self):
        owner=load(self.root)['repository_group']
        probe=self.root/'service-probe.py'
        probe.write_text('import errno,os,sys\n'
            'assert not any(key.startswith("CPKT_OPERATION_") for key in os.environ)\n'
            'for descriptor in map(int,sys.argv[1:]):\n'
            '  try:os.fstat(descriptor)\n'
            '  except OSError as error:assert error.errno==errno.EBADF\n'
            '  else:raise AssertionError("service retained repository ownership")\n'
            'print("service owns no lock descriptors")\n')
        driver=self.root/'service-driver.py'
        driver.write_text('import fcntl,os,subprocess,sys\n'
            'issued=[]\n'
            'for key in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"):\n'
            '  previous=int(os.environ[key]);descriptor=fcntl.fcntl(previous,fcntl.F_DUPFD,20)\n'
            '  os.close(previous);os.environ[key]=str(descriptor);issued.append(descriptor)\n'
            'raise SystemExit(subprocess.call(["bash",sys.argv[1],sys.executable,sys.argv[2],*map(str,issued)],pass_fds=tuple(issued)))\n')
        result=self.command(owner,sys.executable,str(driver),str(self.root/'scripts/service-exec.sh'),str(probe))
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        self.assertIn('service owns no lock descriptors',result.stdout)

    def test_procedural_workflows_reject_forged_operation_before_children(self):
        owner=load(self.root)['repository_group']
        marker=self.root/'unexpected-child'
        for script in ('build.sh','package.sh','require-native-hardening-host.sh'):
            (self.root/'scripts'/script).write_text('#!/usr/bin/env bash\nprintf child > "'+str(marker)+'"\n')
        variables=dict(self.environment,GROUP='all',CPKT_OPERATION_FD='9998',CPKT_OPERATION_CAP_FD='9999',
                       CPKT_OPERATION_ROOT=str(self.root),CPKT_OPERATION_RUN='forged',CPKT_OPERATION_SCOPE='all')
        commands=[('release.sh','prerelease'),('darwin.sh','test-darwin-sdk'),
                  ('memcheck.sh','--group',owner,'--preset','valgrind')]
        if owner=='misc':commands.append(('misc-workflow.sh','cpktxscribe'))
        for script,*arguments in commands:
            with self.subTest(script=script):
                result=subprocess.run(['bash',str(self.root/'scripts'/script),*arguments],env=variables,capture_output=True,text=True)
                self.assertNotEqual(0,result.returncode,result.stdout+result.stderr)
                self.assertIn('operation',result.stderr)
                self.assertFalse(marker.exists(),'forged operation reached child work')

    def _cmake_mutation_observer(self):
        marker=self.root/'native-mutation.json'
        observer=self.root/'cmake-observer'
        observer.write_text('#!'+sys.executable+'\nimport json,pathlib,subprocess,sys\n'
            'arguments=sys.argv[1:]\n'
            'if any(value in arguments for value in ("--preset","--build","--install")):\n'
            '  pathlib.Path('+repr(str(marker))+').write_text(json.dumps(arguments))\n'
            '  raise SystemExit(72)\n'
            'raise SystemExit(subprocess.call(['+repr(shutil.which('cmake'))+',*arguments]))\n')
        observer.chmod(0o755)
        variables={key:value for key,value in self.environment.items() if key not in ('PRESET','CPKT_PRESET')}
        variables['CMAKE']=str(observer)
        return marker,variables

    def test_package_stage_resolves_cli_and_native_environment_presets(self):
        owner=load(self.root)['repository_group']
        marker,variables=self._cmake_mutation_observer()
        (self.root/'VERSION').write_text('0.0.0\n')
        cases=[(['--preset','release'],variables),([],dict(variables,CPKT_PRESET='release'))]
        for selection,environment in cases:
            with self.subTest(selection=selection):
                marker.unlink(missing_ok=True)
                result=self.command(owner,'bash',str(self.root/'scripts/package-stage.sh'),
                    '--group',owner,*selection,env=environment,timeout='5')
                self.assertEqual(result.returncode,72,result.stdout+result.stderr)
                arguments=json.loads(marker.read_text())
                self.assertEqual(arguments[0],'--install')
                self.assertEqual(arguments[1],str(self.root/'build/x86_64-linux-gnu'/owner/'Release'))
        marker.unlink()
        result=self.command(owner,'bash',str(self.root/'scripts/package-stage.sh'),
            '--group',owner,env=variables,timeout='5')
        self.assertNotEqual(result.returncode,0)
        self.assertIn('Release preset required',result.stderr)
        self.assertFalse(marker.exists())

    def test_memcheck_rejects_label_before_preparing_or_building(self):
        owner=load(self.root)['repository_group']
        marker,variables=self._cmake_mutation_observer()
        result=subprocess.run(['bash',str(self.root/'scripts/build.sh'),'memcheck',
            '--group',owner,'--preset','valgrind','--label','nonexistent-label'],
            env=variables,capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('--label requires the test action',result.stderr)
        self.assertFalse(marker.exists())
        self.assertFalse((self.root/'.cache').exists())

    def test_package_stage_launcher_preserves_release_readiness(self):
        owner=load(self.root)['repository_group'];target='x86_64-linux-gnu'
        binary=self.root/'build'/target/owner/'Release';binary.mkdir(parents=True)
        ready=self.root/'build/verification'/target/owner/'Release-development.json';ready.parent.mkdir(parents=True);ready.write_text('passed')
        staged=self.root/'staged';package=self.root/'scripts/package.sh'
        package.write_text('#!/usr/bin/env bash\nprintf "%s\n" "$@" > "$(dirname "$0")/../staged"\n')
        args=['bash',str(self.root/'scripts/build-guard.sh'),str(self.root),owner,'bash',str(package),'package-stage','--group',owner,'--preset','release','--scope','selected']
        result=self.command(owner,*args,cwd=binary)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr);self.assertTrue(ready.exists())
        self.assertIn('package-stage',staged.read_text())
        result=self.command(owner,'bash',str(self.root/'scripts/build-guard.sh'),str(self.root),owner,'bash','-c','true','--check',cwd=binary)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr);self.assertFalse(ready.exists(),'an arbitrary --check flag bypassed invalidation')

    def test_compiler_and_default_temporary_files_stay_in_repository(self):
        code='import os,tempfile,pathlib; p=tempfile.NamedTemporaryFile(delete=False); p.close(); print(p.name); pathlib.Path(p.name).unlink()'
        result=self.command('core',sys.executable,'-c',code)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertTrue(Path(result.stdout.strip()).is_relative_to(self.root/'build/control/tmp'))
        root=self.root/'build/control';(root/'tmp').mkdir(exist_ok=True);(root/'tmp').rename(root/'real-tmp');(root/'tmp').symlink_to(root/'real-tmp',target_is_directory=True)
        result=self.command('core',sys.executable,'-c',code)
        self.assertNotEqual(result.returncode,0);self.assertIn('symlink',result.stderr)

    def test_dist_cleanup_preserves_database_and_build_state(self):
        state=self.root/'build/devenv/data';state.mkdir(parents=True)
        (state/'database').write_bytes(b'important database state')
        dist=self.root/'dist';dist.mkdir();(dist/'package').write_bytes(b'obsolete package')
        before=tree_identity(state)
        result=subprocess.run(['bash',str(self.root/'scripts/clean.sh'),'clean-dist','--group','all'],env=self.environment,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertFalse(dist.exists());self.assertEqual(before,tree_identity(state))

    def test_hardening_configure_revokes_only_its_owned_proof(self):
        shutil.copy(ROOT/'scripts/cpkt_configure_guard.py',self.root/'scripts/cpkt_configure_guard.py')
        target='x86_64-linux-gnu'
        receipts=self.root/'build/verification'/target/'core';receipts.mkdir(parents=True)
        for configuration in ('Debug','Valgrind','Fuzz'):
            for suffix in ('development','built'):
                (receipts/(configuration+'-'+suffix+'.json')).write_bytes(b'existing proof')
        for configuration in ('Valgrind','Fuzz','Debug'):
            before={path.name:path.read_bytes() for path in receipts.iterdir()}
            result=self.command('core',sys.executable,str(self.root/'scripts/cpkt_configure_guard.py'),
                '--root',str(self.root),'--binary',str(self.root/'build'/target/'core'/configuration),
                '--group','core','--target',target,'--configuration','Debug')
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            after={path.name:path.read_bytes() for path in receipts.iterdir()}
            self.assertEqual(after,{name:value for name,value in before.items() if not name.startswith(configuration+'-')})

    def test_inventory_closure_and_ownership(self):
        data = load(self.root)
        for section in ('groups','components','targets','tests'):
            for item in data[section].values():
                for field in ('recipe_inputs','verification_inputs','source_inputs','command_inputs','helper_inputs'):
                    for name in item.get(field,[]):
                        if '${' not in name:
                            self.assertTrue((ROOT/name).is_file(), 'missing inventory input: '+name)
        core = components_for(data, 'core', True)
        self.assertNotIn('postgresql', core)
        self.assertNotIn('whisper', core)
        self.assertEqual({'core'},set(data['groups']))
        self.assertEqual(core,components_for(data,'all',True))
        self.assertFalse(any(item.get('external') for item in data['components'].values()))
        self.assertEqual('core', record(data['targets'], 'cpkt_cmocka_behavior_shared')['group'])
        data['components']['sqlite']={'group':'db','external':'cpkt','dependencies':[]}
        data['components']['openssl']['dependencies'] = ['sqlite']
        (self.root / 'cmake/components.json').write_text(json.dumps(data))
        with self.assertRaisesRegex(RuntimeError, 'forbidden group edge'):
            load(self.root)

    def test_clangd_exact_header_closure_reuse(self):
        binary=self.root/'build/x86_64-linux-gnu/core/Debug';binary.mkdir(parents=True)
        include=self.root/'include with spaces';include.mkdir()
        header=include/'fixture.h';header.write_text('#define FIXTURE_VALUE 1\n')
        source=self.root/'fixture.c';source.write_text('#include "fixture.h"\nint fixture = FIXTURE_VALUE;\n')
        (binary/'compile_commands.json').write_text(json.dumps([{'directory':str(binary),'file':str(source),
            'arguments':[shutil.which('cc'),'-I'+str(include),'-MMD','-MF','forbidden.d','-o','forbidden.o','-c',str(source)]}]))
        counter=self.root/'counter'
        checker=self.root/'checker.py'
        checker.write_text('#!'+sys.executable+'\nfrom pathlib import Path\np=Path('+repr(str(counter))+')\np.write_text(p.read_text()+"x" if p.exists() else "x")\n')
        checker.chmod(0o755)
        command=[sys.executable,str(self.root/'scripts/cpkt_clangd_check.py'),'--root',str(self.root),
            '--build',str(binary),'--group','core','--source',str(source),'--checker',str(checker),'--gate',str(checker)]
        runner=self.root/'hover-run.py'
        runner.write_text('import os,subprocess\nfrom pathlib import Path\n'
            'fds=tuple(int(os.environ[k]) for k in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"))\n'
            'command='+repr(command)+'\n'
            'subprocess.run(command,check=True,pass_fds=fds)\n'
            'subprocess.run(command,check=True,pass_fds=fds)\n'
            'header=Path('+repr(str(header))+')\nstat=header.stat()\nheader.write_text("#define FIXTURE_VALUE 2\\n")\n'
            'os.utime(header,ns=(stat.st_atime_ns,stat.st_mtime_ns))\n'
            'subprocess.run(command,check=True,pass_fds=fds)\n')
        result=self.command('core',sys.executable,str(runner))
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        self.assertEqual('xx',counter.read_text())
        self.assertTrue(list((binary/'clangd-proofs').glob('*/*.json')))
        self.assertFalse((self.root/'build/control/helper-proofs').exists())
        self.assertFalse((self.root/'build/clangd').exists())
        self.assertFalse((binary/'forbidden.d').exists())
        self.assertFalse((binary/'forbidden.o').exists())
        result=self.command('core',*command)
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        self.assertEqual('xxx',counter.read_text())

    def test_built_does_not_supply_readiness(self):
        with self.assertRaisesRegex(RuntimeError, 'make test GROUP=core PRESET=debug'):
            validate_core(self.root, 'x86_64-linux-gnu', 'Debug', 'debug')

    def test_required_test_inventory_cannot_disappear(self):
        target='x86_64-linux-gnu'
        directory=self.root/'build'/target/'core/Debug'
        directory.mkdir(parents=True)
        (directory/'CMakeCache.txt').write_text('CPKT_TARGET_ID:INTERNAL='+target+'\nCMAKE_BUILD_TYPE:STRING=Debug\n')
        receipt=self.root/'build/verification'/target/'core/Debug-development.json'
        receipt.parent.mkdir(parents=True)
        receipt.write_text(json.dumps({'schema_version':1,'status':'passed','kind':'development',
            'coverage':['required'],'group':'core','target':target,'configuration':'Debug'}))
        with self.assertRaisesRegex(RuntimeError,'required test inventory is absent'):
            validate_core(self.root,target,'Debug','debug')

    def test_configured_preset_resolution_and_selector_rejection(self):
        from configured_build import binary_dir
        query=['cmake','-DCPKT_REPO_ROOT='+str(ROOT),'-DCPKT_INFO=components','-P',str(ROOT/'cmake/lifecycle-info.cmake')]
        result=subprocess.run(query,env=dict(self.environment,GROUP='all'),capture_output=True,text=True,check=True)
        self.assertEqual(set(components_for(load(ROOT),'core')),set(result.stdout.splitlines()))
        result=subprocess.run(['bash',str(ROOT/'scripts/build.sh'),'path','--group','misc'],env=self.environment,capture_output=True,text=True)
        self.assertNotEqual(0,result.returncode)
        shutil.copy(ROOT/'CMakePresets.json',self.root/'CMakePresets.json')
        directory=self.root/'build/x86_64-linux-gnu/core/Release'
        directory.mkdir(parents=True)
        cache=directory/'CMakeCache.txt'
        cache.write_text('CPKT_TARGET_ID:INTERNAL=x86_64-linux-gnu\nCPKT_GROUP:STRING=core\n')
        with patch.dict(os.environ,dict(self.environment,GROUP='all',PRESET='release'),clear=True):
            self.assertEqual(directory,binary_dir(self.root,'x86_64-linux-gnu'))
            with self.assertRaisesRegex(RuntimeError,'resolves to'):
                binary_dir(self.root,'aarch64-linux-gnu')
            cache.write_text('CPKT_TARGET_ID:INTERNAL=aarch64-linux-gnu\n')
            with self.assertRaisesRegex(RuntimeError,'configured target does not match'):
                binary_dir(self.root,'x86_64-linux-gnu')
        events=ROOT/'build/control/events.jsonl'
        before=events.read_bytes() if events.is_file() else None
        for arguments,environment in ((['scripts/fuzz.sh','smoke','--group','db'],self.environment),
                (['scripts/fuzz.sh','smoke','--unknown'],self.environment),
                (['bash','scripts/build.sh','build','--group','db','--target','cpkt_pdf_shared'],self.environment),
                (['bash','scripts/clean.sh','clean-dist','--group','db'],self.environment)):
            result=subprocess.run(arguments if arguments[0] in (sys.executable,'bash') else ['bash',*arguments],
                                  cwd=ROOT,env=environment,capture_output=True,text=True)
            self.assertNotEqual(0,result.returncode)
        self.assertEqual(before,events.read_bytes() if events.is_file() else None)

    def test_aggregate_examples_skip_groups_without_example_tests(self):
        owner=load(self.root)['repository_group'];trace=self.root/'trace'
        (self.root/'scripts/build.sh').write_text('#!/usr/bin/env bash\nprintf "%s\n" "$@" > "$(dirname "$0")/../trace"\n')
        result=subprocess.run(['bash',str(self.root/'scripts/lifecycle.sh'),'examples','--group','all','--preset','debug'],env=self.environment,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual(trace.read_text().splitlines(),['test','--regex','example','--group','all','--preset','debug'])
        trace.unlink()
        foreign=next(value for value in ('core','db','misc') if value!=owner)
        result=subprocess.run(['bash',str(self.root/'scripts/lifecycle.sh'),'examples','--group',foreign],env=self.environment,capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0);self.assertFalse(trace.exists())

    def test_public_graph_omissions_link_edges_and_mock_privacy(self):
        for name in ('CpktGroups.cmake','CpktOperation.cmake','CpktPackage.cmake'):
            shutil.copy(ROOT/'cmake'/name,self.root/'cmake'/name)
        shutil.copy(ROOT/'scripts/cpkt_configure_guard.py',self.root/'scripts/cpkt_configure_guard.py')
        data={'schema_version':1,'repository_group':'core','components':{},
            'groups':{'core':{'requires':[]}},
            'targets':{name:{'group':owner,'public':public,'kind':'facade'} for name,owner,public in (
                ('cpkt_public','core',True),('cpkt_optional','misc',False),('cpkt_cmocka_mock','core',False))},
            'tests':{'unused':{'group':'core','execution':'compile','requires':[]}}}
        data['targets']['package-bundle']={'group':'core','public':False,'kind':'add_custom_target'}
        (self.root/'cmake/components.json').write_text(json.dumps(data))
        (self.root/'probe.c').write_text('int probe(void) { return 0; }\n')
        prefix='''cmake_minimum_required(VERSION 3.21)
include(cmake/CpktGroups.cmake)
include(cmake/CpktOperation.cmake)
project(graph C)
set(CPKT_BUILD_TESTS OFF)
add_custom_target(cpkt_operation_guard)
include(cmake/CpktPackage.cmake)
'''
        cases=[('', 'Missing required public inventory target'),
            ('add_library(cpkt_public STATIC probe.c)\nadd_library(cpkt_optional STATIC probe.c)\ntarget_link_libraries(cpkt_public PUBLIC cpkt_optional)\n','Forbidden group linkage'),
            ('add_library(cpkt_public STATIC probe.c)\nadd_library(cpkt_cmocka_mock STATIC probe.c)\ntarget_link_libraries(cpkt_public PUBLIC cpkt_cmocka_mock)\n','links cmocka'),
            ('add_library(cpkt_public STATIC probe.c)\n',None)]
        for index,(body,failure) in enumerate(cases):
            (self.root/'CMakeLists.txt').write_text(prefix+body+'cmake_language(DEFER CALL cpkt_validate_owned_graph)\n')
            result=self.command('core','cmake','-S',str(self.root),'-B',str(self.root/'build'/str(index)),
                                '-DCPKT_GROUP=core')
            if failure:
                self.assertNotEqual(0,result.returncode)
                self.assertIn(failure,result.stdout+result.stderr)
            else:
                self.assertEqual(0,result.returncode,result.stdout+result.stderr)
                available=self.command('core','cmake','--build',str(self.root/'build'/str(index)),'--target','help')
                self.assertEqual(0,available.returncode,available.stdout+available.stderr)
                self.assertIn('package-bundle',available.stdout)
        result=self.command('db','cmake','-S',str(self.root),'-B',
            str(self.root/'build/x86_64-linux-gnu/core/Debug'),'-DCPKT_GROUP=db',
            '-DCPKT_TARGET_ID=x86_64-linux-gnu')
        self.assertNotEqual(0,result.returncode)
        self.assertIn('not owned',result.stdout+result.stderr)

    def test_effective_contract_closure_ignores_unrelated_inputs(self):
        data={'schema_version':1,'repository_group':'core','groups':{'core':{'requires':[]}},
              'components':{},'targets':{},'tests':{}}
        recipe='function(cpkt_common)\n  set(value one)\nendfunction()\n'
        for name,group in [('a','core'),('b','core'),('c','core')]:
            data['components'][name]={'group':group,'directory':name,'dependencies':['a'] if name=='c' else [],
                'helpers':['cpkt_add_'+name]+(['cpkt_common'] if name!='b' else []),'recipe_inputs':[],'variants':['static','shared']}
            recipe+='function(cpkt_add_'+name+')\n  set(value one)\nendfunction()\n'
            recipe+='cpkt_prepare_dependency_component(\n NAME '+name+'\n VARIABLES '+name.upper()+'_PIN\n RECIPE_FUNCTIONS cpkt_add_'+name+')\n'
        (self.root/'cmake/components.json').write_text(json.dumps(data))
        path=self.root/'cmake/CpktDependencies.cmake';path.write_text(recipe)
        top=self.root/'CMakeLists.txt';top.write_text('set(A_PIN one)\nset(B_PIN one)\nset(C_PIN one)\n')
        for group in ('core',):
            cache=self.root/'build/synthetic'/group/'producer/CMakeCache.txt';cache.parent.mkdir(parents=True)
            cache.write_text('CMAKE_GENERATOR:STRING=Ninja\nCPKT_DEPENDENCY_BUILD_JOBS:STRING=2\n')
        def identities():
            script=self.root/'build/contracts.cmake'
            script.write_text('cmake_minimum_required(VERSION 3.21)\nset(CMAKE_SOURCE_DIR "'+str(self.root)+'")\n'
                'set(CMAKE_BINARY_DIR "'+str(self.root/'build')+'")\nset(CPKT_BUILD_DEPENDENCIES ON)\n'
                'set(CPKT_GROUP core)\nset(CPKT_TARGET_ID synthetic)\nset(CPKT_EXTERNAL_ROOT_LIFECYCLE_OWNED ON)\n'
                'set(CPKT_DEPENDENCY_BUILD_ROOT_LIFECYCLE_OWNED ON)\n'
                'set(CPKT_DEPENDENCY_CONTRACT_ROOT "'+str(self.root/'.cache/dependency-contracts')+'")\n'
                'file(READ "'+str(self.root/'cmake/components.json')+'" CPKT_INVENTORY)\n'
                'include("'+str(ROOT/'cmake/CpktDependencyContract.cmake')+'")\n'
                'include("'+str(top)+'")\n'+
                ''.join('cpkt_prepare_dependency_component(NAME '+name+' VARIABLES '+name.upper()+'_PIN '
                    'BUILD_ROOT "'+str(self.root/'.cache/deps-build/synthetic'/name)+'" '
                    'INSTALL_ROOT "'+str(self.root/'.cache/deps/synthetic'/name/'install')+'" '
                    'DEPENDS '+('a' if name=='c' else '')+')\n' for name in data['components']))
            subprocess.run(['cmake','-P',str(script)],check=True,capture_output=True)
            return {name:component_input_id(self.root,'synthetic',name) for name in data['components']}

        before=identities()
        (self.root/'README.md').write_text('unrelated docs')
        for cache in self.root.glob('build/synthetic/*/producer/CMakeCache.txt'):
            cache.write_text('CMAKE_GENERATOR:STRING=Unix Makefiles\nCPKT_DEPENDENCY_BUILD_JOBS:STRING=1\n')
        self.assertEqual(before,identities())
        runtimes=[]
        for variable,name in [('CPKT_CXX_STDLIB_STATIC_LIBRARY','libstdc++.a'),('CPKT_CXX_LIBGCC_STATIC_LIBRARY','libgcc.a')]:
            runtime=self.root/name;runtime.write_bytes(b'original '+name.encode());runtimes.append(runtime)
            for cache in self.root.glob('build/synthetic/*/producer/CMakeCache.txt'):
                with cache.open('a') as stream:stream.write(variable+':FILEPATH='+str(runtime)+'\n')
        before=identities()
        for runtime in runtimes:
            original=runtime.stat();runtime.write_bytes(b'changed '+runtime.name.encode())
            os.utime(runtime,ns=(original.st_atime_ns,original.st_mtime_ns))
            changed=identities()
            for name in before:self.assertNotEqual(before[name],changed[name])
            before=changed
        top.write_text(top.read_text().replace('C_PIN one','C_PIN two'))
        changed=identities()
        self.assertEqual(before['a'],changed['a']);self.assertEqual(before['b'],changed['b'])
        self.assertNotEqual(before['c'],changed['c'])
        path.write_text(recipe.replace('set(value one)','set(value two)',1))
        helpers=identities()
        self.assertNotEqual(changed['a'],helpers['a']);self.assertNotEqual(changed['c'],helpers['c'])
        self.assertEqual(changed['b'],helpers['b'])
        # Core verification fixtures reading the shared recipe file must retain
        # this same relevant closure rather than hash the entire file.
        from cpkt_receipts import verification_inputs
        # The unregistered recipe remains unrelated to this selected graph.
        data['components'].pop('c')
        data['tests']['core_recipe']={'group':'core','execution':'compile',
            'requires':[],'command_inputs':['cmake/CpktDependencies.cmake']}
        (self.root/'cmake/components.json').write_text(json.dumps(data))
        configured={'CPKT_TARGET_ID':'synthetic','CMAKE_BUILD_TYPE':'Debug'}
        before=verification_inputs(self.root,'core',configured)
        path.write_text(path.read_text().replace('function(cpkt_add_c)\n  set(value one)',
                                                'function(cpkt_add_c)\n  set(value unrelated)'))
        self.assertEqual(before,verification_inputs(self.root,'core',configured))
        path.write_text(path.read_text().replace('function(cpkt_add_a)\n  set(value one)',
                                                'function(cpkt_add_a)\n  set(value relevant)'))
        identities()
        self.assertNotEqual(before,verification_inputs(self.root,'core',configured))

    def test_core_verification_tracks_runtime_mock_helper(self):
        from cpkt_receipts import verification_inputs
        data={'schema_version':1,'repository_group':'core',
              'groups':{'core':{'requires':[]}},
              'components':{},'targets':{'cpkt_lua_runtime_mock_test':{'group':'core'}},'tests':{}}
        (self.root/'cmake/components.json').write_text(json.dumps(data))
        top=self.root/'CMakeLists.txt'
        original=(ROOT/'CMakeLists.txt').read_text()
        helper=re.search(r'function\(cpkt_add_lua_runtime_mock_test\b.*?endfunction\(\)',original,re.S)
        self.assertIsNotNone(helper)
        invocation='cpkt_add_lua_runtime_mock_test(cpkt_lua_runtime_mock_test tests/lua_runtime_mock_test.c)'
        data['targets']['cpkt_lua_runtime_mock_test']['source_inputs']=['tests/lua_runtime_mock_test.c']
        (self.root/'tests').mkdir(exist_ok=True)
        (self.root/'tests/lua_runtime_mock_test.c').write_text('original source')
        (self.root/'cmake/components.json').write_text(json.dumps(data))
        top.write_text(helper.group(0)+'\n'+invocation+'\n')
        configured={'CPKT_TARGET_ID':'synthetic','CMAKE_BUILD_TYPE':'Debug'}
        before=verification_inputs(self.root,'core',configured)
        top.write_text(top.read_text().replace('-std=c99','-std=c89'))
        self.assertNotEqual(before,verification_inputs(self.root,'core',configured))
        (self.root/'tests/lua_runtime_mock_test.c').write_text('changed source')
        self.assertNotEqual(before,verification_inputs(self.root,'core',configured))
        self.assertNotIn('db',data['groups'])

    def test_verification_tracks_helper_inputs(self):
        from cpkt_receipts import verification_inputs
        owner=json.loads((ROOT/'cmake/components.json').read_text())['repository_group']
        fixture=self.root/'tests/fixture.c'
        fixture.parent.mkdir(exist_ok=True)
        fixture.write_text('fixture revision one')
        data={'schema_version':1,'repository_group':owner,
              'groups':{owner:{'requires':[] if owner=='core' else ['core']}},'components':{},'targets':{},
              'tests':{'fixture':{'group':'tooling','execution':'helper',
                       'command_inputs':[],'helper_inputs':['tests/fixture.c']}}}
        (self.root/'cmake/components.json').write_text(json.dumps(data))
        (self.root/'CMakeLists.txt').write_text('')
        configured={'CPKT_TARGET_ID':'synthetic','CMAKE_BUILD_TYPE':'Debug'}
        before=verification_inputs(self.root,owner,configured)
        unrelated=self.root/'tests/unowned.c';unrelated.write_text('not selected')
        self.assertEqual(before,verification_inputs(self.root,owner,configured))
        fixture.write_text('fixture revision two')
        self.assertNotEqual(before,verification_inputs(self.root,owner,configured))
        fixture.unlink()
        with self.assertRaisesRegex(RuntimeError,'verification input missing: tests/fixture.c'):
            verification_inputs(self.root,owner,configured)

    def test_verification_tracks_owned_hardening_data(self):
        from cpkt_receipts import verification_inputs
        owner=json.loads((ROOT/'cmake/components.json').read_text())['repository_group']
        suppression=self.root/'tests/check.supp'
        seed=self.root/'fuzz/seeds/input.bin'
        suppression.parent.mkdir(exist_ok=True);seed.parent.mkdir(parents=True)
        suppression.write_text('# no suppressions\n');seed.write_bytes(b'initial')
        data={'schema_version':1,'repository_group':owner,
              'groups':{owner:{'requires':[] if owner=='core' else ['core'],
                              'hardening':{'memcheck_suppression':'tests/check.supp',
                                           'fuzz_seeds':['fuzz/seeds/input.bin']}}},
              'components':{},'targets':{},'tests':{}}
        (self.root/'cmake/components.json').write_text(json.dumps(data))
        (self.root/'CMakeLists.txt').write_text('')
        configured={'CPKT_TARGET_ID':'synthetic','CMAKE_BUILD_TYPE':'Debug'}
        before=verification_inputs(self.root,owner,configured)
        for path in (suppression,seed):
            original=path.read_bytes();path.write_bytes(original+b' changed')
            self.assertNotEqual(before,verification_inputs(self.root,owner,configured))
            path.write_bytes(original)
            self.assertEqual(before,verification_inputs(self.root,owner,configured))
            path.unlink()
            with self.assertRaisesRegex(RuntimeError,'verification input missing'):
                verification_inputs(self.root,owner,configured)
            path.write_bytes(original)

    def test_managed_graph_directory_cannot_be_a_symlink(self):
        owner=load(self.root)['repository_group']
        graph=self.root/'build/x86_64-linux-gnu'/owner/'Release';graph.parent.mkdir(parents=True)
        sibling=self.root/'build/sibling';sibling.mkdir(parents=True);(sibling/'sentinel').write_text('untouched')
        graph.symlink_to(sibling,target_is_directory=True)
        result=subprocess.run(['bash',str(self.root/'scripts/build.sh'),'configure','--group',owner,'--preset','release'],env=self.environment,capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0);self.assertIn('symlink',result.stderr)
        self.assertEqual('untouched',(sibling/'sentinel').read_text());self.assertFalse((self.root/'.cache').exists())

    def test_verification_tracks_owned_abi_and_generator_inputs(self):
        from cpkt_receipts import verification_inputs
        data={'schema_version':1,'repository_group':'core','groups':{'core':{'requires':[]}},
              'components':{},'targets':{},'tests':{}}
        (self.root/'cmake/components.json').write_text(json.dumps(data));(self.root/'CMakeLists.txt').write_text('')
        directory=self.root/'build/x86_64-linux-gnu/core/Debug';directory.mkdir(parents=True)
        definitions=directory/'CTestTestfile.cmake';definitions.write_text('add_test(generated original)')
        configured={'CPKT_TARGET_ID':'x86_64-linux-gnu','CMAKE_BUILD_TYPE':'Debug','CPKT_BUILD_TESTS':'ON',
                    'CPKT_LUA_ABI_VERSION':'0'}
        before=verification_inputs(self.root,'core',configured)
        configured['CPKT_LUA_ABI_VERSION']='1'
        self.assertNotEqual(before,verification_inputs(self.root,'core',configured))
        configured['CPKT_LUA_ABI_VERSION']='0';configured['CPKT_OPCUA_ABI_VERSION']='1'
        self.assertEqual(before,verification_inputs(self.root,'core',configured))
        definitions.write_text('add_test(generated changed-nghttp2-header)')
        self.assertNotEqual(before,verification_inputs(self.root,'core',configured))

    def test_osxcross_link_launcher_preserves_quoted_paths(self):
        original=(ROOT/'CMakeLists.txt').read_text()
        block=re.search(r'if\(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CPKT_OSXCROSS_ROOT\).*?endif\(\)',
                        original,re.S)
        self.assertIsNotNone(block)
        source=self.root/"source's directory"
        source.mkdir()
        osxcross=self.root/"osxcross's directory"
        osxcross.mkdir()
        guard=source/'guard.py'
        guard.write_text('import subprocess,sys\n'
                         'assert sys.argv[1:3]=='+repr([str(source),'core'])+'\n'
                         'raise SystemExit(subprocess.call(sys.argv[3:]))\n')
        launcher_prefix=' '.join(shlex.quote(arg) for arg in
            (sys.executable,str(guard),str(source),'core'))
        script=self.root/'launcher.cmake'
        output=self.root/'launcher.txt'
        script.write_text('set(CMAKE_SYSTEM_NAME Darwin)\n'
                          'set(CPKT_OSXCROSS_ROOT [=['+str(osxcross)+']=])\n'
                          'set(_cpkt_build_launcher [=['+launcher_prefix+']=])\n'
                          +block.group(0)+'\n'
                          'get_property(_launcher GLOBAL PROPERTY RULE_LAUNCH_LINK)\n'
                          'file(WRITE [=['+str(output)+']=] "${_launcher}")\n')
        result=subprocess.run(['cmake','-P',str(script)],capture_output=True,text=True)
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        launcher=output.read_text()
        self.assertEqual([sys.executable,str(guard),str(source),'core',shutil.which('cmake'),'-E','env',
                          'LD_LIBRARY_PATH='+str(osxcross)+'/lib:'+os.environ.get('LD_LIBRARY_PATH','')],
                         shlex.split(launcher))
        command=launcher+' '+shlex.join([sys.executable,'-c',
            'import os,sys; sys.exit(os.environ["LD_LIBRARY_PATH"].split(":")[0] != sys.argv[1])',
            str(osxcross)+'/lib'])
        result=subprocess.run(command,shell=True,capture_output=True,text=True)
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)

    def test_darwin_package_preset_uses_registered_target(self):
        presets=json.loads((ROOT/'CMakePresets.json').read_text())
        selected=next(item for item in presets['buildPresets']
                      if item['name']=='package-arm64-apple-darwin-release')
        self.assertEqual(['package-bundle'],selected['targets'])
        self.assertIn('add_custom_target(package-bundle',
                      (ROOT/'cmake/CpktPackage.cmake').read_text())

    def test_output_content_symlink_and_partial_records(self):
        install = self.root / 'install'
        install.mkdir()
        (install / 'lib.a').write_bytes(b'original')
        (install / 'lib.so').symlink_to('lib.a')
        expected = tree_identity(install)
        before = (install / 'lib.a').stat()
        (install / 'lib.a').write_bytes(b'modified')
        os.utime(install / 'lib.a', ns=(before.st_atime_ns,before.st_mtime_ns))
        self.assertNotEqual(expected, tree_identity(install))
        (install / 'lib.so').unlink()
        (install / 'lib.so').symlink_to('missing')
        with self.assertRaisesRegex(RuntimeError, 'dangling'):
            tree_identity(install)
        path = self.root / 'partial.json'
        path.write_text('{"schema_version":1,"status":"running"}')
        with self.assertRaises(RuntimeError):
            read(path)
        path.write_text('{')
        with self.assertRaises(RuntimeError):
            read(path)

    def test_group_export_metadata_and_linker_alias_outputs(self):
        directory=self.root/'binary';directory.mkdir()
        library=directory/'libprobe.so.0';library.write_bytes(b'library')
        alias=directory/'libprobe.so';alias.symlink_to(library.name)
        exports=directory/'cpkt-facades.cmake';exports.write_text('original import definition')
        (directory/'cpkt-owned-outputs.txt').write_text(str(library)+'\n'+str(alias)+'\n')
        expected=group_outputs(directory)
        self.assertIn('cpkt-facades.cmake',expected)
        stat=exports.stat();exports.write_text('modified import definition')
        os.utime(exports,ns=(stat.st_atime_ns,stat.st_mtime_ns))
        self.assertNotEqual(expected,group_outputs(directory))
        alias.unlink()
        with self.assertRaisesRegex(RuntimeError,'outputs missing/corrupt'):
            group_outputs(directory)

    def test_live_delegation_nested_and_scope_rejection(self):
        check = str(self.root / 'scripts/operation.sh')
        result = self.command('core', sys.executable, check, '--root', str(self.root), '--group', 'core', '--check')
        self.assertEqual(0, result.returncode, result.stderr)
        result = self.command('core', sys.executable, check, '--root', str(self.root), '--group', 'db', '--check')
        self.assertNotEqual(0, result.returncode)
        closed=self.root/'closed-scope.py'
        closed.write_text('import errno,os,subprocess,sys\n'
            'for key in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"):\n'
            '  try:os.close(int(os.environ[key]))\n'
            '  except OSError as error:\n'
            '    if error.errno != errno.EBADF:raise\n'
            'raise SystemExit(subprocess.call(["bash",*sys.argv[1:]]))\n')
        result=self.command('core',sys.executable,str(closed),check,'--root',str(self.root),
                            '--group','db','--check')
        self.assertNotEqual(0,result.returncode,'descriptor recovery widened db into core')

    def test_reopened_fd_and_separate_source_context(self):
        check = str(self.root/'scripts/operation.sh')
        reopened = self.root/'reopened.py'
        reopened.write_text('import os,sys\nsys.path.insert(0,'+repr(str(self.root/'scripts'))+')\n'
            'from cpkt_lock import delegated\n'
            'fd=os.open('+repr(str(self.root/'build/control/operation.lock'))+',os.O_RDWR)\n'
            'os.environ["CPKT_OPERATION_FD"]=str(fd)\n'
            'delegated('+repr(str(self.root))+',"all")\n')
        result = self.command('all',sys.executable,str(reopened))
        self.assertNotEqual(0,result.returncode)
        self.assertIn('not the owning lock description',result.stderr)
        extracted = self.root/'extracted'
        extracted.mkdir(); seed(extracted); (extracted/'CMakeLists.txt').write_text('# separate source context\n')
        child=self.root/'child.py'
        child.write_text('import os,sys\nsys.path.insert(0,'+repr(str(self.root/'scripts'))+')\n'
            'from cpkt_lock import delegated\n'
            'delegated('+repr(str(extracted))+',"all")\n'
            'assert os.environ["CPKT_OPERATION_ROOT"]=='+repr(str(extracted))+'\n')
        result=self.command('all',sys.executable,check,'--root',str(self.root),'--group','all',
            '--source-root',str(extracted),'--',sys.executable,str(child))
        self.assertEqual(0,result.returncode,result.stderr)
        self.assertNotEqual((self.root/'build/control/operation.lock').stat().st_ino,
                            (extracted/'build/control/operation.lock').stat().st_ino)
        result = self.command('all', sys.executable, check, '--root', str(self.root), '--group', 'core', '--',
                              sys.executable, check, '--root', str(self.root), '--group', 'db', '--check')
        self.assertNotEqual(0, result.returncode)
        env = dict(os.environ, CPKT_OPERATION_FD='999', CPKT_OPERATION_ROOT=str(self.root),
                   CPKT_OPERATION_SCOPE='db', CPKT_OPERATION_RUN='fake')
        result = self.command('db', sys.executable, '-c', 'raise SystemExit(0)', env=env)
        self.assertNotEqual(0, result.returncode)
        tamper=self.root/'tamper.py'
        tamper.write_text('import os,sys\nsys.path.insert(0,'+repr(str(self.root/'scripts'))+')\n'
            'from cpkt_lock import delegated\nos.environ["CPKT_OPERATION_SCOPE"]="all"\n'
            'delegated('+repr(str(self.root))+',"all")\n')
        result=self.command('all',sys.executable,check,'--root',str(self.root),'--group','core','--',sys.executable,str(tamper))
        self.assertNotEqual(0,result.returncode)
        self.assertIn('scope string does not match',result.stderr)
        # Native CMake/CTest close unknown descriptors; a narrowed recovery must
        # retain its scope, and forged environment scope must still be rejected.
        closed=self.root/'recover-narrowed.py'
        closed.write_text('import os,sys\n'
            'for key in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"):os.close(int(os.environ[key]))\n'
            'if len(sys.argv)>1:os.environ["CPKT_OPERATION_SCOPE"]="all"\n'
            'sys.path.insert(0,'+repr(str(self.root/'scripts'))+')\n'
            'from cpkt_lock import delegated\ndelegated('+repr(str(self.root))+',"'+'core'+'")\n')
        nested=['bash',check,'--root',str(self.root),'--group','core','--',sys.executable,str(closed)]
        result=self.command('all',*nested)
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        result=self.command('all',*nested,'forge')
        self.assertNotEqual(0,result.returncode)

    def test_stable_inode_bounded_wait_and_owner_interruption(self):
        script = self.root / 'hold.py'
        ready = self.root / 'ready'
        # The parent's pipe keeps the child alive until explicitly released.
        # EOF also releases it if an assertion or the test process fails.
        script.write_text('import os,sys,subprocess\ncode='+repr('import sys; from pathlib import Path; Path('+repr(str(ready))+').write_text("yes"); sys.stdin.buffer.read(1)')+'\nsubprocess.run([sys.executable,"-c",code],start_new_session=True,pass_fds=tuple(int(os.environ[k]) for k in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD")))\n')
        owner = subprocess.Popen(['bash', str(self.root / 'scripts/operation.sh'),
                    '--root', str(self.root), '--group', 'all', '--', sys.executable, str(script)],
                    env=self.environment, stdin=subprocess.PIPE)
        try:
            deadline = time.monotonic()+30
            while not ready.exists() and time.monotonic()<deadline:
                time.sleep(0.01)
            self.assertTrue(ready.exists(), 'operation child did not become ready')
            lock = self.root / 'build/control/operation.lock'
            inode = lock.stat().st_ino
            result = self.command('core', sys.executable, '-c', 'raise SystemExit(0)')
            self.assertNotEqual(0,result.returncode)
            self.assertIn('owner:',result.stderr)
            self.assertEqual(inode,lock.stat().st_ino)
            owner.terminate()
            owner.wait(timeout=10)
            # The explicitly held child retains the inherited lock description.
            result = self.command('core', sys.executable, '-c', 'raise SystemExit(0)',timeout='0.05')
            self.assertNotEqual(0,result.returncode)
            owner.stdin.write(b'x')
            owner.stdin.flush()
            owner.stdin.close()
            result = self.command('core', sys.executable, '-c', 'raise SystemExit(0)',timeout='5')
            self.assertEqual(0,result.returncode,result.stderr)
            self.assertEqual(inode,lock.stat().st_ino)
        finally:
            if not owner.stdin.closed:
                owner.stdin.close()
            if owner.poll() is None:
                owner.terminate()
                owner.wait(timeout=10)

    def test_ctest_failure_stops_later_cases_and_rejects_readiness(self):
        from package_producer_reuse_test import setup, invoke, prerequisite, OWNER
        root,_,source=setup('Ninja',self.root/'failure');prerequisite(root)
        source=source.replace('cpkt_group_add_test(NAME ${CPKT_GROUP}_behavior COMMAND cpkt_${CPKT_GROUP}_probe)',
            'cpkt_group_add_test(NAME ${CPKT_GROUP}_behavior COMMAND cpkt_${CPKT_GROUP}_probe)\n'
            'add_test(NAME later_case COMMAND cmake -E touch "${CMAKE_BINARY_DIR}/later-case")')
        data=json.loads((root/'cmake/components.json').read_text());data['tests']['later_case']={'group':OWNER}
        (root/'cmake/components.json').write_text(json.dumps(data))
        (root/'CMakeLists.txt').write_text(source)
        invoke(root,'test','--preset','debug',env=self.environment)
        directory=root/'build/x86_64-linux-gnu'/OWNER/'Debug'
        marker=directory/'later-case';marker.unlink()
        (root/'main.c').write_text('int main(void) { return 1; }\n')
        failed=invoke(root,'test','--preset','debug',env=self.environment,success=False)
        self.assertFalse(marker.exists(),'selected CTest continued after failure')
        self.assertFalse((root/'build/verification/x86_64-linux-gnu'/OWNER/'Debug-development.json').exists(),failed.stdout+failed.stderr)
        # Composition suites use the same public CTest fail-fast option.
        result=subprocess.run(['ctest','--test-dir',str(directory),'--stop-on-failure'],capture_output=True,text=True,env=self.environment)
        self.assertNotEqual(result.returncode,0,failed.stdout+failed.stderr+result.stdout+result.stderr);self.assertFalse(marker.exists())

    def test_cmake_descriptor_inheritance_and_direct_rejection(self):
        source = self.root / 'source'
        source.mkdir()
        closed=self.root/'closed-descriptors.py'
        closed.write_text('import errno,os,subprocess,sys\n'
            'for key in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"):\n'
            '  try:os.close(int(os.environ[key]))\n'
            '  except OSError as error:\n'
            '    if error.errno != errno.EBADF:raise\n'
            'raise SystemExit(subprocess.call(["bash",*sys.argv[1:]]))\n')
        (source / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(lock NONE)\n'
            'execute_process(COMMAND bash "' + str(self.root / 'scripts/operation.sh') +
            '" --root "' + str(self.root) + '" --group core --check RESULT_VARIABLE status)\n'
            'if(NOT status EQUAL 0)\nmessage(FATAL_ERROR "delegation missing")\nendif()\n')
        result = self.command('core', 'cmake','-S',str(source),'-B',str(self.root/'binary'))
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        (source / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(lock NONE)\n'
            'execute_process(COMMAND "'+sys.executable+'" "'+str(closed)+'" "'
            +str(self.root/'scripts/operation.sh')+'" --root "'+str(self.root)
            +'" --group core --check RESULT_VARIABLE status)\n'
            'if(NOT status EQUAL 0)\nmessage(FATAL_ERROR "delegation missing")\nendif()\n')
        result = self.command('core','cmake','-S',str(source),'-B',str(self.root/'binary-closed'))
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        result = subprocess.run(['cmake','-S',str(source),'-B',str(self.root/'direct')],capture_output=True,text=True)
        self.assertNotEqual(0,result.returncode)

    def test_cmake_test_arguments_and_missing_target_fail_before_build(self):
        shutil.copy(ROOT/'cmake/CpktGroups.cmake',self.root/'cmake/CpktGroups.cmake')
        inventory={'schema_version':1,'repository_group':'core','groups':{'core':{'requires':[]}},'components':{},'targets':{
            'cpkt_probe':{'group':'core','public':False}},'tests':{
            name:{'group':'core','preflight':guarded}
            for name,guarded in (('plain',False),('guarded',True))}}
        (self.root/'cmake/components.json').write_text(json.dumps(inventory))
        working=self.root/'working directory';working.mkdir()
        arguments=['','semi;colon','quote"slash\\dollar${unexpanded}',
                   'brackets ]] ]=] ]==]','\nleading newline','']
        (self.root/'expected.json').write_text(json.dumps(arguments))
        checker=self.root/'argv-check.py'
        checker.write_text('import json,sys\nfrom pathlib import Path\n'
            'root=Path(__file__).resolve().parent\n'
            'assert sys.argv[2:]==json.loads((root/"expected.json").read_text()),repr(sys.argv)\n'
            'assert Path.cwd()==root/"working directory"\n'
            '(root/sys.argv[1]).write_text("passed")\n')
        def literal(value):
            equals=''
            while ']'+equals+']' in value:equals+='='
            return '['+equals+'[\n'+value+']'+equals+']'
        source='cmake_minimum_required(VERSION 3.21)\nproject(arguments NONE)\nenable_testing()\n'
        source+='include(cmake/CpktGroups.cmake)\n'
        source+='set(CPKT_TARGET_ID synthetic)\n'
        header=source
        for name in ('plain','guarded'):
            source+='cpkt_group_add_test(NAME '+name+' COMMAND '
            source+=' '.join(literal(value) for value in [sys.executable,str(checker),name,*arguments])
            source+=' WORKING_DIRECTORY '+literal(str(working))+' CONFIGURATIONS Release)\n'
        (self.root/'CMakeLists.txt').write_text(source)
        binary=self.root/'test-arguments'
        result=subprocess.run(['cmake','-S',str(self.root),'-B',str(binary)],
                              env=self.environment,capture_output=True,text=True)
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        result=self.command('core','ctest','--test-dir',str(binary),'-C','Release','--output-on-failure',
                              env=self.environment)
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        self.assertEqual('passed',(self.root/'plain').read_text())
        self.assertEqual('passed',(self.root/'guarded').read_text())
        (self.root/'CMakeLists.txt').write_text(header+'\ncpkt_group_add_test(NAME plain COMMAND cpkt_probe)\n')
        result=subprocess.run(['cmake','-S',str(self.root),'-B',str(self.root/'missing-target')],
                              env=self.environment,capture_output=True,text=True)
        self.assertNotEqual(0,result.returncode)
        self.assertIn('Required test target is missing: plain: cpkt_probe',result.stderr)

    def test_clean_retains_live_broker_for_later_children(self):
        runner=self.root/'clean-broker.py'
        runner.write_text('import importlib.util,os,sys\nfrom pathlib import Path\n'
            'import subprocess\n'
            'sys.path.insert(0,'+repr(str(ROOT/'scripts'))+')\n'
            'from cpkt_lock import delegated\n'
            'root=Path('+repr(str(self.root))+')\n'
            'subprocess.run(["bash",str(root/"scripts/clean.sh"),"clean","--group","all"],check=True,pass_fds=tuple(int(os.environ[k]) for k in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD")))\n'
            'assert (root/"build/control/.operation.sock").is_socket()\n'
            'assert (root/"build/control/tmp"/os.environ["CPKT_OPERATION_RUN"]).is_dir()\n'
            'for key in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"):\n'
            '  os.close(int(os.environ[key]))\n'
            'delegated(root,"all")\n')
        result=self.command('all',sys.executable,str(runner))
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)

    def test_broker_recovers_in_long_source_reconstruction_root(self):
        extracted=self.root/('reconstructed-source-'*6)
        extracted.mkdir(); seed(extracted)
        self.assertGreater(len(str(extracted/'build/control/.operation.sock').encode()),107)
        closed=self.root/'close-long-root.py'
        closed.write_text('import errno,os,subprocess,sys\n'
            'for key in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"):\n'
            '  try:os.close(int(os.environ[key]))\n'
            '  except OSError as error:\n'
            '    if error.errno != errno.EBADF:raise\n'
            'raise SystemExit(subprocess.call(["bash",*sys.argv[1:]]))\n')
        operation=str(self.root/'scripts/operation.sh')
        result=subprocess.run(['bash',operation,'--root',str(extracted),
            '--group','core','--',sys.executable,str(closed),operation,
            '--root',str(extracted),'--group','core','--check'],
            capture_output=True,text=True,env=self.environment)
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)

    def test_helper_dedup_same_run_and_changed_input(self):
        fixture = self.root / 'fixture'
        fixture.write_text('original')
        counter = self.root / 'count'
        runner = self.root / 'run.py'
        proof = self.root / 'scripts/helper.sh'
        runner.write_text('import subprocess,sys\nfrom pathlib import Path\n'
            'cmd=' + repr(['bash',str(proof),'--root',str(self.root),'--group','core','--mode','fixture','--input',str(fixture),'--',sys.executable,'-c',
                          'from pathlib import Path;p=Path('+repr(str(counter))+');p.write_text(p.read_text()+"x" if p.exists() else "x")']) + '\n'
            'import os\nfds=tuple(int(os.environ[key]) for key in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"))\n'
            'subprocess.run(cmd,check=True,pass_fds=fds)\nsubprocess.run(cmd,check=True,pass_fds=fds)\n'
            'Path(' + repr(str(fixture)) + ').write_text("changed")\nsubprocess.run(cmd,check=True,pass_fds=fds)\n')
        result = self.command('core',sys.executable,str(runner))
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        self.assertEqual('xx',counter.read_text())
        result = self.command('core',sys.executable,str(runner))
        self.assertEqual(0,result.returncode,result.stderr)
        self.assertEqual('xxx',counter.read_text())

    def test_early_helper_ctest_and_direct_execution(self):
        data=load(self.root)
        data['tests']['exact_helper']={'group':'core','execution':'helper','command_inputs':['tests/fixture.py'],
            'requires':[],'preflight':True,'target_sensitive':False,'helper_environment':['PATH']}
        (self.root/'cmake/components.json').write_text(json.dumps(data))
        (self.root/'tests').mkdir()
        counter=self.root/'counter'
        fixture=self.root/'tests/fixture.py'
        fixture.write_text('from pathlib import Path\np=Path('+repr(str(counter))+')\np.write_text(p.read_text()+"x" if p.exists() else "x")\n')
        dispatch=['bash',str(self.root/'scripts/fixture.sh'),'--root',str(self.root),
            '--group','core','--target','synthetic','--test','exact_helper','--binary',str(self.root/'binary'),
            '--',sys.executable,str(fixture)]
        source=self.root/'source';source.mkdir()
        (source/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(helper NONE)\ninclude(CTest)\n'
            'add_test(NAME exact_helper COMMAND '+' '.join('"'+value+'"' for value in dispatch)+')\n')
        subprocess.run(['cmake','-S',str(source),'-B',str(self.root/'binary')],check=True,capture_output=True)
        runner=self.root/'helper-runner.py'
        ctest_wrapper=self.root/'ctest-wrapper.py'
        ctest_wrapper.write_text('#!'+sys.executable+'\nimport os,subprocess,sys\n'
            'from pathlib import Path\n'
            'if "--show-only=json-v1" not in sys.argv:\n'
            '  for key in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"):\n'
            '    os.close(int(os.environ[key]))\n'
            'raise SystemExit(subprocess.call(["ctest",*sys.argv[1:]]))\n')
        ctest_wrapper.chmod(0o755)
        runner.write_text('import os,subprocess,sys\nfrom pathlib import Path\n'
            'sys.path.insert(0,'+repr(str(ROOT/'scripts'))+')\n'

            'fds=tuple(int(os.environ[key]) for key in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"))\n'
            'subprocess.run('+repr(dispatch)+',check=True,pass_fds=fds)\n'
            'subprocess.run(["ctest","--test-dir",'+repr(str(self.root/'binary'))+',"--no-tests=error","--stop-on-failure"],check=True)\n')
        result=self.command('core',sys.executable,str(runner))
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        self.assertEqual('x',counter.read_text())
        result=subprocess.run(['ctest','--test-dir',str(self.root/'binary'),'--no-tests=error','--output-on-failure'],capture_output=True,text=True,env=self.environment)
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        self.assertEqual('xx',counter.read_text())

    def test_exact_junit_inventory_and_disabled_cases_cannot_supply_readiness(self):
        from package_producer_reuse_test import setup,invoke,prerequisite,OWNER
        root,_,_=setup('Ninja',self.root/'junit');prerequisite(root)
        invoke(root,'test','--preset','debug',env=self.environment)
        directory=root/'build/x86_64-linux-gnu'/OWNER/'Debug'
        ready=root/'build/verification/x86_64-linux-gnu'/OWNER/'Debug-development.json'
        inventory=directory/'cpkt-test-inventory.json'
        listing=json.loads(inventory.read_text());results=directory/'cpkt-test-results.xml'
        original=results.read_text();name=listing['tests'][0]['name']
        command=['bash',str(root/'scripts/operation.sh'),'--group',OWNER,'--',sys.executable,str(root/'scripts/cpkt_build_evidence.py'),'tested','--root',str(root),'--group',OWNER,'--target','x86_64-linux-gnu','--configuration','Debug','--preset','debug']
        for xml in ('<testsuite/>','<testsuite><testcase name="'+name+'"/><testcase name="'+name+'"/></testsuite>','<testsuite><testcase name="'+name+'"><skipped/></testcase></testsuite>'):
            results.write_text(xml)
            result=subprocess.run(command,env=self.environment,capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0,result.stdout+result.stderr);self.assertFalse(ready.exists())
        results.write_text(original)
        for field in ('disabled','command','coverage'):
            changed=json.loads(json.dumps(listing))
            if field=='disabled':changed['tests'][0].setdefault('properties',[]).append({'name':'DISABLED','value':True})
            elif field=='command':changed['tests'][0]['command']=[]
            else:changed['tests']=[]
            inventory.write_text(json.dumps(changed))
            result=subprocess.run(command,env=self.environment,capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0,result.stdout+result.stderr);self.assertFalse(ready.exists())
        inventory.write_text(json.dumps(listing))
        result=subprocess.run(command,env=self.environment,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr);self.assertTrue(ready.exists())


if __name__ == '__main__':
    unittest.main()
