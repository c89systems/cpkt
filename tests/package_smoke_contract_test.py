#!/usr/bin/env python3
"""Migrated installed-smoke assertions, exercised through real dispatch/metadata."""
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from contextlib import nullcontext
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import cpkt_sdk_consumer as consumer
from cpkt_inventory import load,validate_inputs
from cpkt_packages import validator,file_records,make_manifest
from native_lifecycle_fixture import metadata,trace,capture,environment,seed
from cpkt_receipts import cache

class Contracts(unittest.TestCase):
    def setUp(self):
        (ROOT/'build/package-isolation-work/fixtures').mkdir(parents=True,exist_ok=True)
        self.tmp=tempfile.TemporaryDirectory(dir=ROOT/'build/package-isolation-work/fixtures');self.work=Path(self.tmp.name)
    def tearDown(self):self.tmp.cleanup()
    def test_native_cmocka_config_preserves_upstream_result_variables(self):
        prefix,graph=metadata(self.work/'sdk')
        config=(prefix/'lib/cmake/cmocka/cmocka-config.cmake').read_text()
        self.assertIn('set(CMOCKA_LIBRARY cmocka::cmocka)',config)
        self.assertIn('set(CMOCKA_LIBRARIES cmocka::cmocka)',config)

    def test_discovery_only_pinned_tools(self):
        report='status=ready\ncc=/verified/bin/gcc\ncxx=/verified/bin/g++\nnm=/verified/bin/nm\nar=/verified/bin/ar\nreadelf=/verified/bin/readelf\nsysroot=/verified/sysroot\n'
        calls=[]
        def invoke(args,**kwargs):calls.append(list(map(str,args)));return report
        with patch.object(consumer,'command',invoke),patch.object(consumer,'ROOT',self.work):
            seed(self.work)
            shutil.copy(ROOT/'CMakePresets.json',self.work/'CMakePresets.json')
            (self.work/'cmake').mkdir(exist_ok=True)
            shutil.copy(ROOT/'cmake/components.json',self.work/'cmake/components.json')
            shutil.copy(ROOT/'CMakeLists.txt',self.work/'CMakeLists.txt')
            actual=consumer.configuration('x86_64-linux-gnu','x86_64-linux-gnu-release')
        self.assertEqual(actual['CMAKE_C_COMPILER'],'/verified/bin/gcc')
        self.assertEqual(actual['CMAKE_READELF'],'/verified/bin/readelf')
        self.assertEqual(calls,[[str(self.work/'scripts/cpkt-toolchains.sh'),'discover','x86_64-linux-gnu']])
    def test_real_consumer_plan_flags_pic_prefix_and_isolation(self):
        data=load(ROOT);prefix=self.work/'sdk';prefix.mkdir();(prefix/'include').mkdir()
        configured={'CMAKE_C_COMPILER':'/verified/gcc','CMAKE_CXX_COMPILER':'/verified/g++','CPKT_DEPENDENCY_BUILD_JOBS':'8'}
        records={k:v for k,v in data['installed_consumers'].items() if v['group']=='core'}
        calls=[]
        def invoke(args,**kwargs):
            calls.append((list(map(str,args)),kwargs))
            build=self.work/'plan/build'
            if '--build' in list(map(str,args)):
                for name,item in records.items():
                    path=build/'CMakeFiles'/f'{name}.dir/link.txt';path.parent.mkdir(parents=True,exist_ok=True);path.write_text(' '.join(s.replace('@prefix',str(prefix)) for s in item['link_contains']))
            return ''
        with patch.object(consumer,'command',invoke):consumer.configure_consumer(prefix,self.work/'plan','x86_64-linux-gnu',configured,records,data)
        text=(self.work/'plan/source/CMakeLists.txt').read_text()
        self.assertIn('NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH',text)
        self.assertIn('-std=c89;-pedantic-errors;-Wall;-Wextra;-Werror',text)
        self.assertIn('add_library(cpkt_cmake_pic_',text)
        self.assertIn('cpkt_use_local_runtime(',text)
        self.assertIn('-Wall -Wextra -Wpedantic -Werror',text)
        self.assertIn('CpktLocalRuntime.cmake',text)
        self.assertIn('cmocka upstream discovery result variables are missing',text)
        self.assertIn('target_link_libraries(cmocka_native_shared PRIVATE ${CMOCKA_LIBRARIES})',text)
        self.assertIn('--parallel',calls[1][0]);self.assertIn('8',calls[1][0])
        self.assertIn('CpktReadOnlyToolchain.cmake',' '.join(calls[0][0]))
        self.assertEqual(calls[0][1]['env']['CPKT_RESOLVED_TARGET'],'x86_64-linux-gnu')
    def test_pkg_config_host_decoy(self):
        package=self.work/'package';host=self.work/'host';package.mkdir();host.mkdir()
        for directory,name in ((package,'package'),(host,'host')):
            (directory/'isolation.pc').write_text('Name: isolation\nDescription: fixture\nVersion: 1.0\nLibs: -l'+name+'\n')
        env=dict(os.environ,PKG_CONFIG_PATH=str(host),PKG_CONFIG_LIBDIR=str(package))
        polluted=subprocess.check_output(['pkg-config','--libs','isolation'],env=env,text=True)
        self.assertIn('-lhost',polluted)
        env['PKG_CONFIG_PATH']=''
        actual=subprocess.check_output(['pkg-config','--libs','isolation'],env=env,text=True)
        self.assertIn('-lpackage',actual);self.assertNotIn('-lhost',actual)
        text=(ROOT/'scripts/cpkt_sdk_consumer.py').read_text();self.assertIn("'PKG_CONFIG_PATH':''",text);self.assertIn("prefix/'share/cpkt/validate-sdk.py'",text)
    def test_all_original_consumer_sources_and_closures(self):
        data=load(ROOT);validate_inputs(ROOT,data)
        for item in data['installed_consumers'].values():self.assertTrue((ROOT/item['source']).is_file())
        snippets='\n'.join((ROOT/item['source']).read_text() for item in data['installed_consumers'].values())
        for value in ({'core':('cpkt_openssl_', 'cpkt_gss_', 'cpkt_sasl_'), 'db':('cpkt_postgres_', 'cpkt_sqlite_', 'SQLGetDiagRec'), 'misc':('cpkt_opcua_server_new_from_json','cpkt_sus_segmented_config','transcribe_audio_decoder_segmented_text','segmented_config.prebuffer_ms = 50UL','strcmp(entry.name, "tiny")')}[data['repository_group']]):
            self.assertIn(value,snippets)
        for value in ('cpkt_sus_realtime','cpkt_sus_model_config','cpkt_sus_model_open_path'):self.assertNotIn(value,snippets)
        pic=[v for v in data['installed_consumers'].values() if v['kind']=='pic'];self.assertGreaterEqual(len(pic),len([facade for component in data['components'].values() if not component.get('external') for facade in component['package']['facades']]))
        original=(ROOT/'cmake/package_metadata.cmake').read_text().replace('\\$','$')
        for framework in ('CoreFoundation','CoreServices','Security','SystemConfiguration'):self.assertIn('-framework '+framework,original)
        self.assertIn('CURL::libcurl;m;${CMAKE_DL_LIBS};Threads::Threads',original)
        if data['repository_group']=='misc':self.assertIn('cpkt_sus_mixed_cxx',data['installed_consumers'])
        for relative,item in data['installed_examples'].items():
            self.assertIn(relative+'/CMakeLists.txt',data['groups'][item['group']]['package']['examples'])
        self.assertIn("delivered=prefix/'share/doc/cpkt'",(ROOT/'scripts/cpkt_sdk_examples.py').read_text())
    def test_recipe_scope_and_order(self):
        root=self.work/'release';variables,record=trace(root)
        result=subprocess.run(['bash',str(root/'scripts/lifecycle.sh'),'release'],env=variables,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        calls=[json.loads(v)['args'] for v in record.read_text().splitlines()]
        self.assertEqual(calls[0][0],'version-contract.sh');self.assertEqual(calls[1][0],'clean.sh')
        self.assertLess(next(i for i,v in enumerate(calls) if 'preflight' in v),next(i for i,v in enumerate(calls) if 'test' in v))
        self.assertLess(next(i for i,v in enumerate(calls) if 'format' in v),next(i for i,v in enumerate(calls) if 'source-archive-verify.sh' in v))
        packages=[v for v in calls if v[0]=='package.sh']
        self.assertEqual(len(packages),3)
        self.assertIn('binary',packages[0]);self.assertIn('release',packages[1]);self.assertIn('release',packages[2])
        result=subprocess.run(['bash',str(root/'scripts/lifecycle.sh'),'prerelease-live'],env=dict(variables,CPKT_LIVE_CHECKS='0'),capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0)

    def test_job_defaults_and_explicit_limits(self):
        root=self.work/'jobs';variables,record=capture(root)
        owner=load(ROOT)['repository_group'];directory=root/'build/x86_64-linux-gnu'/owner/'Release';directory.mkdir(parents=True)
        def jobs(value=None,darwin=False):
            env=dict(variables)
            for key in ('CPKT_DEPENDENCY_BUILD_JOBS','CMAKE_BUILD_PARALLEL_LEVEL'):env.pop(key,None)
            if value is not None:env['CPKT_DEPENDENCY_BUILD_JOBS']=value
            if darwin:
                tools=root/'build/test-tools';(tools/'uname').write_text('#!/bin/sh\nprintf "Darwin\\n"\n');(tools/'uname').chmod(0o755)
                env['PATH']=str(tools)+os.pathsep+env['PATH']
            return subprocess.run(['bash','-euc','source "$1"; cpkt_jobs "$2"','fixture',str(root/'scripts/lifecycle-common.sh'),str(directory)],env=env,capture_output=True,text=True)
        self.assertEqual(jobs().stdout.strip(),'8')
        (directory/'CMakeCache.txt').write_text('CPKT_DEPENDENCY_BUILD_JOBS:STRING=3\n');self.assertEqual(jobs().stdout.strip(),'3')
        self.assertEqual(jobs('6').stdout.strip(),'6');self.assertNotEqual(jobs('9').returncode,0)
        (directory/'CMakeCache.txt').unlink();self.assertEqual(jobs(darwin=True).stdout.strip(),'2')
        self.assertNotEqual(jobs('3',True).returncode,0);self.assertEqual(jobs('1',True).stdout.strip(),'1')
        # Preset job values are evaluated by CMake, then respected by Bash.
        from package_producer_reuse_test import setup,invoke,OWNER
        root2,presets,_=setup('Ninja',self.work/'preset-jobs')
        presets['configurePresets'][0]['cacheVariables']['CPKT_DEPENDENCY_BUILD_JOBS']='5'
        (root2/'CMakePresets.json').write_text(json.dumps(presets))
        invoke(root2,'deps','--preset','debug',env=environment())
        self.assertEqual(cache(root2/'build/x86_64-linux-gnu'/OWNER/'producer/CMakeCache.txt')['CPKT_DEPENDENCY_BUILD_JOBS'],'5')
        producer=root2/'build/x86_64-linux-gnu'/OWNER/'producer/CMakeCache.txt'
        producer.write_text(re.sub(r'(CPKT_DEPENDENCY_BUILD_JOBS:[^=\n]+=)5',r'\g<1>2',producer.read_text()))
        invoke(root2,'deps','--preset','debug',env=environment())
        self.assertEqual(cache(producer)['CPKT_DEPENDENCY_BUILD_JOBS'],'2')
        invoke(root2,'configure','--preset','debug',env=environment())
        consumer=root2/'build/x86_64-linux-gnu'/OWNER/'Debug/CMakeCache.txt'
        self.assertEqual(cache(consumer)['CPKT_DEPENDENCY_BUILD_JOBS'],'5')
        self.assertEqual(cache(producer)['CPKT_DEPENDENCY_BUILD_JOBS'],'2')
        consumer.write_text(re.sub(r'(CPKT_DEPENDENCY_BUILD_JOBS:[^=\n]+=)5',r'\g<1>3',consumer.read_text()))
        invoke(root2,'configure','--preset','debug',env=environment())
        self.assertEqual(cache(consumer)['CPKT_DEPENDENCY_BUILD_JOBS'],'3')
        self.assertEqual(cache(producer)['CPKT_DEPENDENCY_BUILD_JOBS'],'2')


if __name__=='__main__':unittest.main()
