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
from cpkt_operation import run, delegated


class Isolation(unittest.TestCase):
    def setUp(self):
        self.environment={key:value for key,value in os.environ.items() if not key.startswith('CPKT_OPERATION_')}
        (ROOT / 'build').mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='isolation-regression-', dir=ROOT / 'build')
        self.root = Path(self.temp.name)
        (self.root / 'cmake').mkdir()
        shutil.copy(ROOT / 'cmake/components.json', self.root / 'cmake/components.json')
        (self.root / 'scripts').mkdir()
        for name in ('cpkt_operation.py','cpkt_inventory.py','cpkt_receipts.py','cpkt_helper_proof.py','cpkt_helper_dispatch.py','cpkt_cmake_inputs.py','cpkt_clangd_check.py','cpkt_make_program.py','cpkt_build_guard.py'):
            shutil.copy(ROOT / 'scripts' / name, self.root / 'scripts' / name)

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
        return subprocess.run([sys.executable, str(self.root / 'scripts/cpkt_operation.py'),
            '--root', str(self.root), '--group', group, '--timeout', timeout, '--', *arguments],
            capture_output=True, text=True, env=env if env is not None else self.environment, cwd=cwd)

    def test_package_stage_launcher_preserves_release_readiness(self):
        target='x86_64-linux-gnu';group='core'
        binary=self.root/'build'/target/group/'Release';binary.mkdir(parents=True)
        (binary/'CMakeCache.txt').write_text('CPKT_TARGET_ID:INTERNAL='+target+'\n'
            'CPKT_GROUP:STRING='+group+'\nCMAKE_BUILD_TYPE:STRING=Release\n')
        ready=self.root/'build/verification'/target/group/'Release-development.json'
        ready.parent.mkdir(parents=True);ready.write_text('passed')
        staged=self.root/'staged.txt'
        package=self.root/'scripts/cpkt_packages.py'
        package.write_text('import pathlib,sys\npathlib.Path('+repr(str(staged))+').write_text(" ".join(sys.argv[1:]))\n')
        guard=str(self.root/'scripts/cpkt_build_guard.py')
        result=self.command(group,sys.executable,guard,str(self.root),group,
                            sys.executable,str(package),'stage','--group',group,'--preset','release',
                            cwd=binary)
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        self.assertTrue(ready.exists())
        self.assertEqual('stage --group core --preset release',staged.read_text())
        ready.write_text('passed')
        result=self.command(group,sys.executable,guard,str(self.root),group,
                            sys.executable,str(package),'stage','--group','db','--preset','release',
                            cwd=binary)
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        self.assertFalse(ready.exists(),'a different group cannot retain this graph readiness')

    def test_compiler_and_default_temporary_files_stay_in_repository(self):
        code='import os,tempfile,pathlib; p=tempfile.NamedTemporaryFile(delete=False); p.close(); print(p.name); pathlib.Path(p.name).unlink()'
        result=self.command('core',sys.executable,'-c',code)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertTrue(Path(result.stdout.strip()).is_relative_to(self.root/'build/control/tmp'))
        root=self.root/'build/control';(root/'tmp').mkdir();(root/'tmp').rename(root/'real-tmp');(root/'tmp').symlink_to(root/'real-tmp',target_is_directory=True)
        result=self.command('core',sys.executable,'-c',code)
        self.assertNotEqual(result.returncode,0);self.assertIn('temporary ancestry',result.stderr)

    def test_dist_cleanup_preserves_database_and_build_state(self):
        module_spec=importlib.util.spec_from_file_location('dist_cleanup_backend',ROOT/'scripts/group-build.py')
        module=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(module)
        state=self.root/'build/devenv/data';state.mkdir(parents=True)
        (state/'database').write_bytes(b'important database state')
        dist=self.root/'dist';dist.mkdir();(dist/'package').write_bytes(b'obsolete package')
        before=tree_identity(self.root/'build')
        with patch.object(module,'ROOT',self.root),patch.object(module,'command') as command:
            module.clean('all',dist_only=True)
        self.assertFalse(dist.exists())
        self.assertEqual(before,tree_identity(self.root/'build'))
        command.assert_not_called()

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
        query=[sys.executable,str(ROOT/'scripts/cpkt_inventory_cli.py')]
        result=subprocess.run(query,env=dict(self.environment,GROUP='all'),capture_output=True,text=True,check=True)
        self.assertEqual(set(components_for(load(ROOT),'core')),set(json.loads(result.stdout)['components']))
        result=subprocess.run(query+['--group','misc'],env=self.environment,capture_output=True,text=True)
        self.assertNotEqual(0,result.returncode)
        shutil.copy(ROOT/'CMakePresets.json',self.root/'CMakePresets.json')
        directory=self.root/'build/x86_64-linux-gnu/core/Release'
        directory.mkdir(parents=True)
        cache=directory/'CMakeCache.txt'
        cache.write_text('CPKT_TARGET_ID:INTERNAL=x86_64-linux-gnu\nCPKT_GROUP:STRING=core\n')
        with patch.dict(os.environ,{'GROUP':'all','PRESET':'release'},clear=True):
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
                ([sys.executable,'scripts/group-build.py','build','--group','db','--target','cpkt_pdf_shared'],self.environment),
                ([sys.executable,'scripts/group-build.py','clean','--group','db','--dist-only'],self.environment)):
            result=subprocess.run(arguments if arguments[0]==sys.executable else ['bash',*arguments],
                                  cwd=ROOT,env=environment,capture_output=True,text=True)
            self.assertNotEqual(0,result.returncode)
        self.assertEqual(before,events.read_bytes() if events.is_file() else None)

    def test_aggregate_examples_skip_groups_without_example_tests(self):
        module_spec=importlib.util.spec_from_file_location('lifecycle_backend',ROOT/'scripts/cpkt_lifecycle.py')
        module=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(module)
        with patch.object(module,'backend') as backend:
            module.perform('examples','all','debug',False,None,False,None)
            self.assertEqual([('test','core','debug','--regex','example')],
                             [call.args for call in backend.call_args_list])
            backend.reset_mock()
            with self.assertRaisesRegex(ValueError,'no example tests for GROUP=db'):
                module.perform('examples','db','debug',True,None,False,None)
            backend.assert_not_called()

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
        self.assertIn('invalid choice',result.stdout+result.stderr)

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
        identities=lambda:{name:component_input_id(self.root,'synthetic',name) for name in ('a','b','c')}
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
        top.write_text(helper.group(0)+'\n'+invocation+'\n')
        configured={'CPKT_TARGET_ID':'synthetic','CMAKE_BUILD_TYPE':'Debug'}
        before=verification_inputs(self.root,'core',configured)
        top.write_text(top.read_text().replace('-std=c99','-std=c89'))
        self.assertNotEqual(before,verification_inputs(self.root,'core',configured))
        top.write_text(helper.group(0)+'\n'+invocation.replace('lua_runtime_mock_test.c','lua_facade_test.c')+'\n')
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
        module_spec=importlib.util.spec_from_file_location('group_build_path_fixture',ROOT/'scripts/group-build.py')
        module=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(module)
        graph=self.root/'build/x86_64-linux-gnu/core/Release'
        graph.parent.mkdir(parents=True)
        sibling=self.root/'build/sibling';sibling.mkdir(parents=True)
        graph.symlink_to(sibling,target_is_directory=True)
        with patch.object(module,'ROOT',self.root):
            with self.assertRaisesRegex(RuntimeError,'symlink'):
                module.owned_path(graph)
            graph.unlink()
            self.assertEqual(graph,module.owned_path(graph))

    def test_verification_tracks_owned_abi_and_generator_inputs(self):
        from cpkt_receipts import verification_inputs
        top=ROOT/'CMakeLists.txt'
        original=top.read_text()
        configured={'CPKT_TARGET_ID':'x86_64-linux-gnu','CMAKE_BUILD_TYPE':'Debug','CPKT_BUILD_TESTS':'ON'}
        read_text=Path.read_text
        with patch('cpkt_receipts.component_input_id',return_value='unchanged recipe'):
            before=verification_inputs(ROOT,'core',configured)
            changes=(
                (original.replace('set(CPKT_LUA_ABI_VERSION "0"','set(CPKT_LUA_ABI_VERSION "1"'),True),
                (original.replace('--include-dir "$<TARGET_PROPERTY:cpkt::nghttp2_static,INTERFACE_INCLUDE_DIRECTORIES>"',
                                  '--include-dir "/changed/nghttp2/header"'),True),
                ('set(CPKT_OPCUA_ABI_VERSION "1")\n'+original,False))
            for changed,affects_core in changes:
                self.assertTrue(original!=changed,"test mutation must change the configured declaration")
                def replacement(path,*args,**kwargs):
                    return changed if path==top else read_text(path,*args,**kwargs)
                with patch.object(Path,'read_text',replacement):
                    self.assertEqual(affects_core,verification_inputs(ROOT,'core',configured)!=before)

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
        check = str(self.root / 'scripts/cpkt_operation.py')
        result = self.command('core', sys.executable, check, '--root', str(self.root), '--group', 'core', '--check')
        self.assertEqual(0, result.returncode, result.stderr)
        result = self.command('core', sys.executable, check, '--root', str(self.root), '--group', 'db', '--check')
        self.assertNotEqual(0, result.returncode)
        closed=self.root/'closed-scope.py'
        closed.write_text('import os,subprocess,sys\n'
            'for key in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"):\n'
            '  os.close(int(os.environ[key]))\n'
            'raise SystemExit(subprocess.call([sys.executable,*sys.argv[1:]]))\n')
        result=self.command('core',sys.executable,str(closed),check,'--root',str(self.root),
                            '--group','db','--check')
        self.assertNotEqual(0,result.returncode,'descriptor recovery widened db into core')

    def test_reopened_fd_and_separate_source_context(self):
        check = str(self.root/'scripts/cpkt_operation.py')
        reopened = self.root/'reopened.py'
        reopened.write_text('import os,sys\nsys.path.insert(0,'+repr(str(self.root/'scripts'))+')\n'
            'from cpkt_operation import delegated\n'
            'fd=os.open('+repr(str(self.root/'build/control/operation.lock'))+',os.O_RDWR)\n'
            'os.environ["CPKT_OPERATION_FD"]=str(fd)\n'
            'delegated('+repr(str(self.root))+',"all")\n')
        result = self.command('all',sys.executable,str(reopened))
        self.assertNotEqual(0,result.returncode)
        self.assertIn('not the owning lock description',result.stderr)
        extracted = self.root/'extracted'
        extracted.mkdir(); (extracted/'CMakeLists.txt').write_text('# separate source context\n')
        child=self.root/'child.py'
        child.write_text('import os,sys\nsys.path.insert(0,'+repr(str(self.root/'scripts'))+')\n'
            'from cpkt_operation import delegated\n'
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
            'from cpkt_operation import delegated\nos.environ["CPKT_OPERATION_SCOPE"]="all"\n'
            'delegated('+repr(str(self.root))+',"all")\n')
        result=self.command('all',sys.executable,check,'--root',str(self.root),'--group','core','--',sys.executable,str(tamper))
        self.assertNotEqual(0,result.returncode)
        self.assertIn('scope string does not match',result.stderr)

    def test_stable_inode_bounded_wait_and_owner_interruption(self):
        script = self.root / 'hold.py'
        script.write_text('import os,time\nfrom pathlib import Path\nPath("' + str(self.root / 'ready') + '").write_text("yes")\ntime.sleep(2)\n')
        owner = subprocess.Popen([sys.executable, str(self.root / 'scripts/cpkt_operation.py'),
                    '--root', str(self.root), '--group', 'all', '--', sys.executable, str(script)],env=self.environment)
        try:
            deadline = time.monotonic()+2
            while not (self.root / 'ready').exists() and time.monotonic()<deadline:
                time.sleep(0.01)
            lock = self.root / 'build/control/operation.lock'
            inode = lock.stat().st_ino
            result = self.command('core', sys.executable, '-c', 'raise SystemExit(0)')
            self.assertNotEqual(0,result.returncode)
            self.assertIn('owner:',result.stderr)
            self.assertEqual(inode,lock.stat().st_ino)
            owner.terminate()
            owner.wait()
            # The child still owns the inherited description after interruption.
            result = self.command('core', sys.executable, '-c', 'raise SystemExit(0)',timeout='0.05')
            self.assertNotEqual(0,result.returncode)
            time.sleep(2)
            result = self.command('core', sys.executable, '-c', 'raise SystemExit(0)')
            self.assertEqual(0,result.returncode,result.stderr)
            self.assertEqual(inode,lock.stat().st_ino)
        finally:
            if owner.poll() is None:
                owner.terminate()
                owner.wait()

    def test_cmake_descriptor_inheritance_and_direct_rejection(self):
        source = self.root / 'source'
        source.mkdir()
        closed=self.root/'closed-descriptors.py'
        closed.write_text('import os,subprocess,sys\n'
            'for key in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"):\n'
            '  os.close(int(os.environ[key]))\n'
            'raise SystemExit(subprocess.call([sys.executable,*sys.argv[1:]]))\n')
        (source / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(lock NONE)\n'
            'execute_process(COMMAND "' + sys.executable + '" "' + str(self.root / 'scripts/cpkt_operation.py') +
            '" --root "' + str(self.root) + '" --group core --check RESULT_VARIABLE status)\n'
            'if(NOT status EQUAL 0)\nmessage(FATAL_ERROR "delegation missing")\nendif()\n')
        result = self.command('core', 'cmake','-S',str(source),'-B',str(self.root/'binary'))
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        (source / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(lock NONE)\n'
            'execute_process(COMMAND "'+sys.executable+'" "'+str(closed)+'" "'
            +str(self.root/'scripts/cpkt_operation.py')+'" --root "'+str(self.root)
            +'" --group core --check RESULT_VARIABLE status)\n'
            'if(NOT status EQUAL 0)\nmessage(FATAL_ERROR "delegation missing")\nendif()\n')
        result = self.command('core','cmake','-S',str(source),'-B',str(self.root/'binary-closed'))
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        result = subprocess.run(['cmake','-S',str(source),'-B',str(self.root/'direct')],capture_output=True,text=True)
        self.assertNotEqual(0,result.returncode)

    def test_clean_retains_live_broker_for_later_children(self):
        runner=self.root/'clean-broker.py'
        runner.write_text('import importlib.util,os,sys\nfrom pathlib import Path\n'
            'from unittest.mock import patch\n'
            'sys.path.insert(0,'+repr(str(ROOT/'scripts'))+')\n'
            'from cpkt_operation import delegated\n'
            'spec=importlib.util.spec_from_file_location("clean_backend",'+repr(str(ROOT/'scripts/group-build.py'))+')\n'
            'module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)\n'
            'root=Path('+repr(str(self.root))+')\n'
            'with patch.object(module,"ROOT",root):module.clean("all")\n'
            'assert (root/"build/control/.operation.sock").is_socket()\n'
            'assert (root/"build/control/tmp"/os.environ["CPKT_OPERATION_RUN"]).is_dir()\n'
            'for key in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"):\n'
            '  os.close(int(os.environ[key]))\n'
            'delegated(root,"all")\n')
        result=self.command('all',sys.executable,str(runner))
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)

    def test_broker_recovers_in_long_source_reconstruction_root(self):
        extracted=self.root/('reconstructed-source-'*6)
        extracted.mkdir()
        self.assertGreater(len(str(extracted/'build/control/.operation.sock').encode()),107)
        closed=self.root/'close-long-root.py'
        closed.write_text('import os,subprocess,sys\n'
            'for key in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"):\n'
            '  os.close(int(os.environ[key]))\n'
            'raise SystemExit(subprocess.call([sys.executable,*sys.argv[1:]]))\n')
        operation=str(self.root/'scripts/cpkt_operation.py')
        result=subprocess.run([sys.executable,operation,'--root',str(extracted),
            '--group','core','--',sys.executable,str(closed),operation,
            '--root',str(extracted),'--group','core','--check'],
            capture_output=True,text=True,env=self.environment)
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)

    def test_helper_dedup_same_run_and_changed_input(self):
        fixture = self.root / 'fixture'
        fixture.write_text('original')
        counter = self.root / 'count'
        runner = self.root / 'run.py'
        proof = self.root / 'scripts/cpkt_helper_proof.py'
        runner.write_text('import subprocess,sys\nfrom pathlib import Path\n'
            'cmd=' + repr([sys.executable,str(proof),'--root',str(self.root),'--group','core','--mode','fixture','--input',str(fixture),'--',sys.executable,'-c',
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
        dispatch=[sys.executable,str(self.root/'scripts/cpkt_helper_dispatch.py'),'--root',str(self.root),
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
            'from cpkt_preflight_runner import run_registered_fixtures\n'
            'fds=tuple(int(os.environ[key]) for key in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"))\n'
            'subprocess.run('+repr(dispatch)+',check=True,pass_fds=fds)\n'
            'run_registered_fixtures(Path('+repr(str(self.root/'binary'))+'),["exact_helper"],'
            'ctest='+repr(str(ctest_wrapper))+')\n')
        result=self.command('core',sys.executable,str(runner))
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        self.assertEqual('x',counter.read_text())
        subprocess.run(['ctest','--test-dir',str(self.root/'binary'),'--no-tests=error'],check=True,capture_output=True,env=self.environment)
        self.assertEqual('xx',counter.read_text())


if __name__ == '__main__':
    unittest.main()
