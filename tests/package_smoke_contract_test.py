#!/usr/bin/env python3
"""Migrated installed-smoke assertions, exercised through real dispatch/metadata."""
import importlib.util
import json
import os
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
import cpkt_lifecycle as lifecycle
from cpkt_inventory import load,validate_inputs
from cpkt_packages import metadata,validator,file_records,make_manifest
from cpkt_receipts import cache

class Contracts(unittest.TestCase):
    def setUp(self):
        (ROOT/'build/package-isolation-work/fixtures').mkdir(parents=True,exist_ok=True)
        self.tmp=tempfile.TemporaryDirectory(dir=ROOT/'build/package-isolation-work/fixtures');self.work=Path(self.tmp.name)
    def tearDown(self):self.tmp.cleanup()
    def test_native_cmocka_config_preserves_upstream_result_variables(self):
        prefix=self.work/'sdk'
        (prefix/'lib/cmake').mkdir(parents=True)
        (prefix/'lib/pkgconfig').mkdir(parents=True)
        configured={'CPKT_TARGET_ID':'x86_64-linux-gnu',
            'CPKT_CMOCKA_VERSION':'1.1.7','CPKT_BUNDLE_VERSION':'0.12.0'}
        with patch('cpkt_packages.command',return_value=''):
            metadata(prefix,self.work,configured,load(ROOT),'core')
        config=(prefix/'lib/cmake/cmocka/cmocka-config.cmake').read_text()
        self.assertIn('set(CMOCKA_LIBRARY cmocka::cmocka)',config)
        self.assertIn('set(CMOCKA_LIBRARIES cmocka::cmocka)',config)
    def test_discovery_only_pinned_tools(self):
        report='status=ready\ncc=/verified/bin/gcc\ncxx=/verified/bin/g++\nnm=/verified/bin/nm\nar=/verified/bin/ar\nreadelf=/verified/bin/readelf\nsysroot=/verified/sysroot\n'
        calls=[]
        def invoke(args,**kwargs):calls.append(list(map(str,args)));return report
        with patch.object(consumer,'command',invoke),patch.object(consumer,'ROOT',self.work):
            shutil.copy(ROOT/'CMakePresets.json',self.work/'CMakePresets.json')
            (self.work/'cmake').mkdir()
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
        steps=[]
        def command(args,group):steps.append(('command',list(map(str,args)),group))
        def backend(action,group,preset,*extra):steps.append(('backend',action,group,preset,list(extra)))
        with patch.object(lifecycle,'command',command),patch.object(lifecycle,'backend',backend),patch.object(lifecycle.subprocess,'check_output',return_value='0.0.0\n'),patch.object(lifecycle,'child_delegation',return_value=nullcontext(({},()))),patch.object(lifecycle.subprocess,'run',side_effect=lambda args,**kwargs:steps.append(('command',list(map(str,args)), 'db'))):
            lifecycle.perform('release','all','debug',False,'',False,'')
        self.assertIn('cpkt_reserved_tag.py',' '.join(steps[0][1]));self.assertEqual(steps[1][1],'clean')
        own=[item for item in steps if item[0]=='backend']
        self.assertLess(next(n for n,v in enumerate(own) if v[1]=='preflight'),next(n for n,v in enumerate(own) if v[1]=='test'))
        commands=[' '.join(item[1]) for item in steps if item[0]=='command']
        self.assertLess(next(n for n,v in enumerate(commands) if 'format' in v),next(n for n,v in enumerate(commands) if 'source-archive-verify' in v))
        source=[value for value in commands if 'package-source.sh' in value];self.assertEqual(len(source),1)
        package=[value for value in commands if 'cpkt_packages.py' in value];self.assertEqual(len(package),3)
        self.assertIn('package --group all --scope binary',package[0]);self.assertIn('--scope release',package[1]);self.assertIn('--scope release',package[2])
        with self.assertRaises(ValueError),patch.dict(os.environ,{'CPKT_LIVE_CHECKS':'0'}):lifecycle.perform('prerelease-live','all','debug',False,'',False,'')
    def test_job_defaults_and_explicit_limits(self):
        spec=importlib.util.spec_from_file_location('group_backend',ROOT/'scripts/group-build.py');backend=importlib.util.module_from_spec(spec);spec.loader.exec_module(backend)
        directory=self.work/'graph';directory.mkdir()
        selected={'cacheVariables':{}}
        with patch.object(backend,'preset_info',return_value=(selected,'x86_64-linux-gnu','Release')),patch.dict(os.environ,{},clear=True),patch.object(backend.sys,'platform','linux'):
            self.assertIn('-DCPKT_DEPENDENCY_BUILD_JOBS=8',backend.configure_command('release','core',directory))
            (directory/'CMakeCache.txt').write_text('CPKT_DEPENDENCY_BUILD_JOBS:STRING=3\n')
            self.assertIn('-DCPKT_DEPENDENCY_BUILD_JOBS=3',backend.configure_command('release','core',directory))
            with patch.dict(os.environ,{'CPKT_DEPENDENCY_BUILD_JOBS':'6'}):self.assertIn('-DCPKT_DEPENDENCY_BUILD_JOBS=6',backend.configure_command('release','core',directory))
            (directory/'CMakeCache.txt').unlink();selected['cacheVariables']['CPKT_DEPENDENCY_BUILD_JOBS']='5'
            self.assertIn('-DCPKT_DEPENDENCY_BUILD_JOBS=5',backend.configure_command('release','core',directory))
            selected['cacheVariables'].clear()
            with patch.object(backend.sys,'platform','darwin'):self.assertIn('-DCPKT_DEPENDENCY_BUILD_JOBS=2',backend.configure_command('native','core',directory))
            with patch.dict(os.environ,{'CPKT_DEPENDENCY_BUILD_JOBS':'9'}),self.assertRaises(RuntimeError):backend.configure_command('release','core',directory)
            with patch.object(backend.sys,'platform','darwin'),patch.dict(os.environ,{'CPKT_DEPENDENCY_BUILD_JOBS':'3'}),self.assertRaises(RuntimeError):backend.configure_command('native','core',directory)
            with patch.object(backend.sys,'platform','darwin'),patch.dict(os.environ,{'CPKT_DEPENDENCY_BUILD_JOBS':'1'}):self.assertIn('-DCPKT_DEPENDENCY_BUILD_JOBS=1',backend.configure_command('native','core',directory))

if __name__=='__main__':unittest.main()
