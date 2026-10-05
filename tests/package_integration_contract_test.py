#!/usr/bin/env python3
"""Independent negative SDK/transport/scope contracts; no dependency producers."""
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import urllib.error
import zipfile
from contextlib import nullcontext
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from cpkt_packages import validator, safe_extract, artifacts, check_snapshot, abi_records
from cpkt_github_handoff import GitHub, acquire, draft_matches, validate_handoff, download_handoff, dispatch_identity, REPOSITORY
from cpkt_presets import preset_info
from cpkt_reserved_tag import create, recover, git


def encoded(value):
    return json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode()

def digest(payload):return hashlib.sha256(payload).hexdigest()

class Fixtures(unittest.TestCase):
    def setUp(self):
        (ROOT/'build/package-isolation-work/fixtures').mkdir(parents=True,exist_ok=True)
        self.tmp=tempfile.TemporaryDirectory(dir=ROOT/'build/package-isolation-work/fixtures')
        self.work=Path(self.tmp.name)
    def tearDown(self):self.tmp.cleanup()
    def fails(self,operation):
        with self.assertRaises((ValueError,OSError,RuntimeError,KeyError,TypeError)):operation()
    def sdk(self):
        prefix=self.work/'sdk';prefix.mkdir()
        catalog={'core':['core/*'],'db':['db/*'],'misc':['misc/*']}
        (prefix/'share/cpkt').mkdir(parents=True)
        (prefix/'share/cpkt/payload-ownership.json').write_bytes(encoded(catalog))
        (prefix/'share/cpkt/payload-ownership.json').chmod(0o644)
        manifests={}
        for group in ('core','db','misc'):
            (prefix/group).mkdir();file=prefix/group/'payload';file.write_bytes(group.encode());file.chmod(0o644)
            link=prefix/group/'link';link.symlink_to('payload')
            names=[group+'/link',group+'/payload']
            if group=='core':names+=['share/cpkt/payload-ownership.json']
            files=[]
            for name in sorted(names):
                path=prefix/name
                files.append({'path':name,'type':'symlink','target':os.readlink(path)} if path.is_symlink() else {'path':name,'type':'file','mode':'0644','sha256':digest(path.read_bytes())})
            manifest={'schema_version':1,'group':group,'release_version':'1.2.3','target_id':'x86_64-linux-gnu','libc':'gnu','macos_deployment_target':None,'components':[{'name':group+'-native','version':'9.2','source_sha256':'a'*64,'features':{'static':True},'abi':{'soname':'lib'+group+'.so.4'}}],'files':files,'requires_core':None if group=='core' else {k:manifests['core'][k] for k in ('package_id','release_version','target_id')}}
            manifest['package_id']=digest(encoded(manifest));manifests[group]=manifest
            self.write_manifest(prefix,group,manifest)
        return prefix,manifests
    def write_manifest(self,prefix,group,manifest,resign=False):
        if resign:
            manifest=dict(manifest);manifest.pop('package_id',None);manifest['package_id']=digest(encoded(manifest))
        path=prefix/'share/cpkt/packages'/f'{group}.json';path.parent.mkdir(exist_ok=True);path.write_bytes(encoded(manifest));path.chmod(0o644);return manifest


    def test_installed_pkg_config_example_preserves_darwin_runtime_arguments(self):
        import cpkt_sdk_examples as examples
        root=self.work/'example root';root.mkdir()
        prefix=root/'SDK with spaces';relative='examples/fixture'
        delivered=prefix/'share/doc/cpkt/core'/relative;delivered.mkdir(parents=True)
        (delivered/'CMakeLists.txt').write_text('fixture')
        scripts=sorted((ROOT/'examples').glob('*/build-pkg-config.sh'))
        if scripts:shutil.copy2(scripts[0],delivered/'build-pkg-config.sh')
        else:
            (delivered/'build-pkg-config.sh').write_text(
                '#!/bin/sh\nset -eu\noutput=$1; shift\nexec "$CC" -o "$output" "$@"\n')
            (delivered/'build-pkg-config.sh').chmod(0o755)
        compiler=root/'selected compiler'
        compiler.write_text('#!'+sys.executable+'\nimport json,pathlib,sys\n'
            'args=sys.argv[1:]\npathlib.Path(args[args.index("-o")+1]).write_text(json.dumps(args))\n')
        compiler.chmod(0o755)
        pkg=root/'pkg-config';pkg.write_text('#!/bin/sh\nexit 0\n');pkg.chmod(0o755)
        data={'components':{},'installed_examples':{relative:{'group':'core',
            'target':'fixture_example','runtime_args':[],'pkg_config_script':'build-pkg-config.sh'}}}
        configured={'CMAKE_C_COMPILER':str(compiler),'CMAKE_CXX_COMPILER':str(compiler),
            'CPKT_DEPENDENCY_BUILD_JOBS':'1'}
        phase=root/'phase';phase.mkdir();calls=[]
        def invoke(args,env=None,**kwargs):
            calls.append(list(map(str,args)))
            if 'installed example configure' in args:
                build=Path(args[args.index('-B')+1]);build.mkdir()
                (build/'runtime-flags.txt').write_text('')
            elif 'installed pkg-config example' in args:
                environment=dict(os.environ,**env,PKG_CONFIG=str(pkg))
                result=subprocess.run(list(map(str,args)),env=environment,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            return ''
        def execute(binary,args,target,config):
            if binary.name.endswith('-pkg'):
                words=json.loads(binary.read_text())
                self.assertIn('-Wl,-rpath,'+str(prefix/'lib'),words)
                self.assertIn('-mmacosx-version-min=15.0',words)
                self.assertFalse(any(word.startswith("'") for word in words))
            return {'status':'passed'}
        with patch.object(examples,'command',side_effect=invoke),patch.object(sys,'platform','darwin'):
            results=examples.run_examples(prefix,'arm64-apple-darwin',configured,data,['core'],phase,execute)
        self.assertEqual(len(results),2)
        self.assertEqual(len([call for call in calls if 'installed pkg-config example' in call]),1)

    def test_darwin_library_duplicates_preserve_static_archive_order(self):
        from cpkt_sdk_consumer import darwin_link_libraries
        words=['-L/sdk/lib','-lcpkt_lua','/sdk/lib/backend.a','-ldl','-lm',
            '/sdk/lib/backend.a','-lcpkt_lua','-ldl','-pthread','-lm']
        self.assertEqual(darwin_link_libraries(words),['-L/sdk/lib','/sdk/lib/backend.a',
            '/sdk/lib/backend.a','-lcpkt_lua','-ldl','-pthread','-lm'])

    def test_warning_gate_rejects_successful_compiler_link_warnings(self):
        warning=self.work/'linker.py'
        warning.write_text('import sys\nprint("ld: warning: ignoring duplicate libraries: -ldl",file=sys.stderr)\n')
        result=subprocess.run(['bash',str(ROOT/'scripts/run-no-warnings.sh'),
            'installed pkg-config consumer',sys.executable,str(warning)],capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('installed pkg-config consumer emitted warnings',result.stderr)


    def test_raw_pkg_config_consumer_stops_before_execution_on_link_warning(self):
        import cpkt_sdk_consumer as consumer
        compiler=self.work/'compiler'
        compiler.write_text('#!'+sys.executable+'\nimport sys\n'
            'print("ld: warning: ignoring duplicate libraries: -ldl",file=sys.stderr)\n')
        compiler.chmod(0o755)
        root=self.work/'consumer';root.mkdir();prefix=root/'sdk';prefix.mkdir()
        (root/'scripts').mkdir();shutil.copy2(ROOT/'scripts/run-no-warnings.sh',root/'scripts/run-no-warnings.sh')
        build=root/'compiled';build.mkdir();(build/'runtime-flags.txt').write_text('')
        configured={'CMAKE_C_COMPILER':str(compiler),'CMAKE_OSX_SYSROOT':'/selected/SDK'}
        records={'fixture':{'group':'core','kind':'shared','pc':'fixture',
            'standard':89,'source':'fixture.c','runtime_args':[]}}
        def invoke(args,**kwargs):
            if args[0]=='pkg-config':return '-lcpkt_lua -ldl -ldl'
            if 'installed pkg-config consumer' in args or args[0]==str(compiler):
                result=subprocess.run(list(map(str,args)),capture_output=True,text=True)
                if result.returncode:raise ValueError(result.stdout+result.stderr)
            return ''
        # CMake execution is stubbed; exercise the real raw-link warning gate.
        with patch.object(consumer,'ROOT',root),patch.object(consumer,'load',return_value={'installed_consumers':{}}),patch.object(consumer,'configuration',return_value=configured),patch.object(consumer,'composition_records',return_value=records),patch.object(consumer,'inspect'),patch.object(consumer,'configure_consumer',return_value=build),patch.object(consumer,'execute'),patch.object(consumer,'file_records',return_value={}),patch.object(consumer.validator,'validate'),patch.object(consumer,'command',side_effect=invoke),patch.object(sys,'platform','darwin'):
            with self.assertRaisesRegex(ValueError,'installed pkg-config consumer emitted warnings'):
                consumer.run_consumers(prefix,'arm64-apple-darwin','fixture',['core'],['core'],composition=True)

    def test_native_producer_and_consumer_share_selected_tools(self):
        import cpkt_sdk_consumer as consumer
        from cpkt_inventory import REPOSITORY_GROUP
        spec=importlib.util.spec_from_file_location('native_backend',ROOT/'scripts/group-build.py')
        backend=importlib.util.module_from_spec(spec);spec.loader.exec_module(backend)
        root=self.work/'native-source';(root/'cmake').mkdir(parents=True)
        for name in ('CMakeLists.txt','CMakePresets.json','cmake/components.json'):
            shutil.copy2(ROOT/name,root/name)
        tools=root/'selected Xcode';tools.mkdir()
        sdk=tools/'MacOSX.sdk';sdk.mkdir()
        for name in ('clang','clang++','nm','ar','otool'):(tools/name).write_bytes(b'selected tool')
        def discover(args,**kwargs):
            if args==['xcrun','--show-sdk-path']:return str(sdk)+'\n'
            self.assertEqual(args[:2],['xcrun','--find'])
            return str(tools/args[2])+'\n'
        def backend_command(args,*context):return discover(args)
        preset='arm64-apple-darwin-native';target='arm64-apple-darwin'
        directory=root/'build'/target/REPOSITORY_GROUP/'Release';directory.mkdir(parents=True)
        with patch.object(backend,'ROOT',root),patch.object(consumer,'ROOT',root),\
             patch.object(consumer.sys,'platform','darwin'),\
             patch.object(consumer,'command',side_effect=discover),\
             patch.object(backend,'command',side_effect=backend_command),\
             patch.dict(os.environ,{'CPKT_DEPENDENCY_BUILD_JOBS':'2'}):
            selected=consumer.configuration(target,preset)
            backend.environment(preset,REPOSITORY_GROUP)
            inherited=subprocess.check_output([sys.executable,'-c',
                'import os; print(os.environ["SDKROOT"])'],text=True).strip()
            self.assertEqual(selected['CMAKE_OSX_SYSROOT'],inherited)
            keys=('CMAKE_C_COMPILER','CMAKE_CXX_COMPILER','CMAKE_NM','CMAKE_AR',
                  'CMAKE_OTOOL','CMAKE_OSX_SYSROOT')
            for producer in (False,True):
                args=backend.configure_command(preset,REPOSITORY_GROUP,directory,producer)
                actual={arg[2:].split(':',1)[0]:arg.split('=',1)[1]
                        for arg in map(str,args) if arg.startswith('-D') and '=' in arg}
                self.assertEqual({key:selected[key] for key in keys},
                                 {key:actual[key] for key in keys})
            item,_,_=backend.preset_info(preset)
            values={key:backend.preset_cache_value(value,preset)
                    for key,value in item['cacheVariables'].items()}
            values.update(backend.producer_flags(preset,REPOSITORY_GROUP))
            values.update({key:selected[key] for key in keys})
            def write(values):
                (directory/'CMakeCache.txt').write_text(''.join(
                    key+':STRING='+value+'\n' for key,value in values.items()))
            write(values)
            self.assertFalse(backend.requested_producer_change(preset,REPOSITORY_GROUP,directory))
            self.assertEqual(selected['CMAKE_C_COMPILER'],consumer.configuration(target,preset)['CMAKE_C_COMPILER'])
            for key in keys:
                changed=dict(values);changed[key]=str(root/'other-tool-or-sdk')
                write(changed)
                self.assertTrue(backend.requested_producer_change(preset,REPOSITORY_GROUP,directory))
                with self.assertRaisesRegex(ValueError,'configured '+key+' differs'):
                    consumer.configuration(target,preset)
            write(values)

    def test_native_darwin_lanes_export_sdk_to_child_commands(self):
        import cpkt_darwin as darwin
        sdk=self.work/'selected SDK/MacOSX.sdk';sdk.mkdir(parents=True)
        def discovery(args,**kwargs):
            if args==['xcrun','--show-sdk-path']:return str(sdk)+'\n'
            self.assertEqual(args[:2],['xcrun','--find'])
            return '/selected-Xcode/'+args[2]+'\n'
        def child():
            inherited=subprocess.check_output([sys.executable,'-c',
                'import os; print(os.environ["SDKROOT"])'],text=True).strip()
            self.assertEqual(str(sdk),inherited)
        for action,entry in (('source','native_source'),('sdk','native_sdk')):
            with self.subTest(action=action),patch.object(darwin.sys,'platform','darwin'),\
                 patch.object(darwin.sys,'argv',['cpkt_darwin.py',action]),\
                 patch.dict(os.environ,{'CPKT_OPERATION_FD':'1','SDKROOT':'wrong-sdk'}),\
                 patch.object(darwin,'delegated'),\
                 patch.object(darwin,'command',side_effect=discovery),\
                 patch.object(darwin,entry,side_effect=child):
                darwin.main()

    def test_package_metadata_does_not_replay_parent_diagnostic_controls(self):
        import cpkt_packages as packages
        from cpkt_inventory import REPOSITORY_GROUP
        prefix=self.work/'metadata-prefix';(prefix/'lib/pkgconfig').mkdir(parents=True)
        directory=self.work/'metadata-work';directory.mkdir()
        configured={'CPKT_TARGET_ID':'arm64-apple-darwin','CPKT_BUNDLE_VERSION':'1.2.3',
                    'CPKT_CMOCKA_VERSION':'2.0.0','CMAKE_WARN_DEPRECATED':'TRUE',
                    'CMAKE_ERROR_DEPRECATED':'FALSE'}
        def execute(args,**kwargs):
            result=subprocess.run(list(map(str,args)),capture_output=True,text=True)
            self.assertEqual(0,result.returncode,result.stdout+result.stderr)
            self.assertNotIn('CMP0218',result.stderr)
        with patch.object(packages,'package_inventory',return_value=([],[],[])),\
             patch.object(packages,'command',side_effect=execute):
            packages.metadata(prefix,directory,configured,{},REPOSITORY_GROUP)
        script=(directory/'package-metadata-input.cmake').read_text()
        self.assertNotIn('CMAKE_WARN_DEPRECATED',script)
        self.assertNotIn('CMAKE_ERROR_DEPRECATED',script)
        self.assertIn('CPKT_BUNDLE_VERSION',script)

    def test_cold_artifact_facade_abi_checks_without_producer_cache(self):
        import cpkt_sdk_consumer as consumer
        root=self.work/'cold-source';(root/'cmake').mkdir(parents=True)
        for name in ('CMakeLists.txt','CMakePresets.json','cmake/components.json'):
            shutil.copy2(ROOT/name,root/name)
        tools=root/'tools';tools.mkdir()
        sdk=root/'sdk';sdk.mkdir()
        for name in ('clang','clang++','nm','ar','otool'):(tools/name).write_bytes(b'fixture tool')
        def discover(args,**kwargs):
            if args==['xcrun','--show-sdk-path']:return str(sdk)+'\n'
            self.assertEqual(args[:2],['xcrun','--find'])
            return str(tools/args[2])+'\n'
        with patch.object(consumer,'ROOT',root),patch.object(consumer.sys,'platform','darwin'),patch.object(consumer,'command',side_effect=discover),patch.dict(os.environ,{'CPKT_DEPENDENCY_BUILD_JOBS':'2'}):
            configured=consumer.configuration('arm64-apple-darwin','arm64-apple-darwin-native')
        variables={f['abi_version_variable'] for c in json.loads((root/'cmake/components.json').read_text())['components'].values() for f in c['package']['facades'] if f.get('abi_version_variable')}
        self.assertEqual({key:configured[key] for key in variables},{key:'0' for key in variables})
        self.assertEqual(configured['CMAKE_OSX_SYSROOT'],str(sdk))
        self.assertFalse((root/'build').exists())
        if sys.platform!='linux':return
        # Both fixtures have accurate manifests; only the source ABI contract
        # distinguishes the incompatible library from the permitted one.
        prefix=self.work/'abi-sdk';(prefix/'lib').mkdir(parents=True)
        workspace=self.work/'abi-inspection';workspace.mkdir()
        source=self.work/'abi.c';source.write_text('void cpkt_fixture_call(void) {}\n')
        obj=self.work/'abi.o';cc=shutil.which('cc');ar=shutil.which('ar')
        subprocess.run([cc,'-fPIC','-c',str(source),'-o',str(obj)],check=True,capture_output=True)
        subprocess.run([ar,'qc',str(prefix/'lib/libcpkt_fixture.a'),str(obj)],check=True,capture_output=True)
        (prefix/'lib/libcpkt_fixture.so').symlink_to('libcpkt_fixture.so.0')
        config=dict(configured,CPKT_TARGET_ID='x86_64-linux-gnu',CMAKE_NM=shutil.which('nm'),CMAKE_AR=ar,CMAKE_READELF=shutil.which('readelf'))
        data={'components':{'fixture':{'group':'core','package':{'facades':[{'target':'cpkt_fixture','abi_version_variable':'CPKT_OPENSSL_ABI_VERSION','export_policy':'^cpkt_fixture_call$'}]}}}}
        def run(args,capture=False,**kwargs):
            result=subprocess.run(list(map(str,args)),capture_output=True,text=True)
            if result.returncode:raise ValueError(result.stdout+result.stderr)
            return result.stdout if capture else None
        for abi in ('0','1'):
            subprocess.run([cc,'-shared','-fPIC',str(source),'-Wl,-soname,libcpkt_fixture.so.'+abi,'-o',str(prefix/'lib/libcpkt_fixture.so.0')],check=True,capture_output=True)
            with patch.object(consumer,'command',side_effect=run),patch('cpkt_packages.command',side_effect=run):
                actual=abi_records(prefix,config)
                manifest={'components':[{'abi':actual}]}
                with patch.object(consumer,'inspect_notices'),patch.object(validator,'load_manifest',return_value=manifest):
                    if abi=='0':consumer.inspect(prefix,'x86_64-linux-gnu',config,data,['core'],workspace)
                    else:
                        with self.assertRaisesRegex(ValueError,'SONAME|soname'):
                            consumer.inspect(prefix,'x86_64-linux-gnu',config,data,['core'],workspace)

    def test_owned_parent_traversal_rejected_without_sibling_mutation(self):
        import cpkt_packages as packages
        root=self.work/'repo';(root/'build').mkdir(parents=True)
        sibling=self.work/'outside';sibling.mkdir();sentinel=sibling/'untouched';sentinel.write_bytes(b'original')
        with patch.object(packages,'ROOT',root):
            for path in (root/'build/../../outside/file',root/'../outside/file'):
                with self.assertRaisesRegex(ValueError,'parent traversal'):packages.write_json(path,{'bad':True})
            packages.write_json(root/'build/owned.json',{'ok':True})
        self.assertEqual(list(sibling.iterdir()),[sentinel]);self.assertEqual(sentinel.read_bytes(),b'original')

    def test_owned_consumer_identity_tracks_static_runtime_archive_bytes(self):
        from cpkt_packages import consumer_context
        runtime=self.work/'libstdc++.a';runtime.write_bytes(b'original static runtime')
        configured={'CPKT_CXX_STDLIB_STATIC_LIBRARY':str(runtime)}
        context=lambda:consumer_context(ROOT,'x86_64-linux-gnu','x86_64-linux-gnu-release','core',{'core':'a'*64},configured)
        before=context();stat=runtime.stat();runtime.write_bytes(b'modified static runtime')
        os.utime(runtime,ns=(stat.st_atime_ns,stat.st_mtime_ns))
        self.assertNotEqual(before,context())

    def test_darwin_runtime_identity_tracks_frameworks_and_host_clang(self):
        from cpkt_receipts import tool_runtime_inputs,darwin_backend_inputs,darwin_backend_paths
        sdk=self.work/'sdk';header=sdk/'System/Library/Frameworks/Fixture.framework/Headers/api.h'
        header.parent.mkdir(parents=True);header.write_bytes(b'first framework header')
        (sdk/'SDKSettings.json').write_bytes(b'first SDK settings')
        before=copy.deepcopy(tool_runtime_inputs('',str(sdk)))
        stat=header.stat();header.write_bytes(b'changed framework header')
        os.utime(header,ns=(stat.st_atime_ns,stat.st_mtime_ns));tool_runtime_inputs.cache_clear()
        self.assertNotEqual(before,tool_runtime_inputs('',str(sdk)))
        before=copy.deepcopy(tool_runtime_inputs('',str(sdk)))
        (sdk/'SDKSettings.json').write_bytes(b'changed SDK settings');tool_runtime_inputs.cache_clear()
        self.assertNotEqual(before,tool_runtime_inputs('',str(sdk)))
        backend=self.work/'host-clang';backend.write_bytes(b'first host compiler')
        resources=self.work/'clang-resources';resources.mkdir();builtin=resources/'stddef.h';builtin.write_bytes(b'first builtin header')
        driver=self.work/'osxcross-driver'
        driver.write_text('#!'+sys.executable+'\nimport sys\nprint('+repr('"'+str(backend)+'" "-cc1" "-resource-dir" "'+str(resources)+'"')+',file=sys.stderr)\n')
        driver.chmod(0o755);configured={'CMAKE_C_COMPILER':str(driver)}
        first=darwin_backend_inputs(configured)
        stat=backend.stat();backend.write_bytes(b'changed host compiler');os.utime(backend,ns=(stat.st_atime_ns,stat.st_mtime_ns))
        second=darwin_backend_inputs(configured);self.assertNotEqual(first,second)
        builtin.write_bytes(b'changed builtin header');self.assertNotEqual(second,darwin_backend_inputs(configured))
        driver.write_text('#!'+sys.executable+'\n');driver.chmod(0o755);darwin_backend_paths.cache_clear()
        with self.assertRaisesRegex(RuntimeError,'compiler/resource identity'):darwin_backend_inputs(configured)
        tool_runtime_inputs.cache_clear();darwin_backend_paths.cache_clear()

    def test_smoke_zip_compressed_deterministic_and_preserves_delivered_bytes(self):
        from cpkt_darwin import write_smoke_archive,extract_smoke
        package=self.work/'smoke';(package/'bin').mkdir(parents=True)
        program=package/'bin/program';program.write_bytes(b'delivered executable\n'*10000);program.chmod(0o755)
        (package/'bin/link').symlink_to('program')
        archives=[self.work/'smoke-one.zip',self.work/'smoke-two.zip']
        for path in archives:write_smoke_archive(package,path)
        self.assertEqual(archives[0].read_bytes(),archives[1].read_bytes())
        with zipfile.ZipFile(archives[0]) as archive:
            for item in archive.infolist():self.assertEqual(item.compress_type,zipfile.ZIP_DEFLATED)
            item=archive.getinfo('darwin-smoke-test/bin/program')
            self.assertLess(item.compress_size,item.file_size//10)
        destination=self.work/'extracted';extract_smoke(archives[0],destination)
        self.assertEqual((destination/'darwin-smoke-test/bin/program').read_bytes(),program.read_bytes())
        self.assertEqual((destination/'darwin-smoke-test/bin/program').stat().st_mode & 0o777,0o755)
        self.assertEqual(os.readlink(destination/'darwin-smoke-test/bin/link'),'program')
    def test_runtime_loaded_library_cannot_fall_back_to_sysroot(self):
        from cpkt_sdk_consumer import validate_runtime_resolution
        prefix=self.work/'runtime-sdk';(prefix/'lib').mkdir(parents=True)
        delivered=prefix/'lib/libcrypto.so.3';delivered.write_bytes(b'delivered bytes')
        sysroot=self.work/'sysroot';(sysroot/'lib').mkdir(parents=True)
        loader=sysroot/'lib/ld-linux-x86-64.so.2';loader.write_bytes(b'target loader')
        host=sysroot/'lib/libcrypto.so.3';host.write_bytes(b'wrong library')
        configured={'CMAKE_READELF':sys.executable,'CMAKE_SYSROOT':str(sysroot),'CPKT_INSTALLED_PREFIX':str(prefix)}
        def output(args,**kwargs):
            if args[1]=='-l':return 'Requesting program interpreter: /lib/ld-linux-x86-64.so.2]'
            return 'libcrypto.so.3 => '+str(host)+' (0x123)'
        with patch('cpkt_sdk_consumer.command',side_effect=output):
            self.fails(lambda:validate_runtime_resolution('consumer','x86_64-linux-gnu',configured,[]))
        def selected(args,**kwargs):
            if args[1]=='-l':return 'Requesting program interpreter: /lib/ld-linux-x86-64.so.2]'
            return 'libcrypto.so.3 => '+str(delivered)+' (0x123)'
        with patch('cpkt_sdk_consumer.command',side_effect=selected):
            self.assertEqual(validate_runtime_resolution('consumer','x86_64-linux-gnu',configured,[])[0]['path'],str(delivered))
        outside=self.work/'host/libc.so.6';outside.parent.mkdir();outside.write_bytes(b'foreign host runtime')
        def foreign(args,**kwargs):
            if args[1]=='-l':return 'Requesting program interpreter: /lib/ld-linux-x86-64.so.2]'
            return 'libc.so.6 => '+str(outside)+' (0x123)'
        with patch('cpkt_sdk_consumer.command',side_effect=foreign):
            with self.assertRaisesRegex(ValueError,'host runtime outside verified'):
                validate_runtime_resolution('consumer','x86_64-linux-gnu',configured,[])

    def test_runtime_execution_and_inspection_share_verified_loader_path(self):
        from cpkt_sdk_consumer import execute
        prefix=self.work/'execution-sdk';(prefix/'lib').mkdir(parents=True)
        sysroot=self.work/'execution-sysroot';(sysroot/'lib').mkdir(parents=True)
        loader=sysroot/'lib/ld-linux-x86-64.so.2';loader.write_bytes(b'target loader')
        binary=self.work/'execution-consumer';binary.write_bytes(b'consumer')
        configured={'CMAKE_READELF':sys.executable,'CMAKE_SYSROOT':str(sysroot),'CPKT_INSTALLED_PREFIX':str(prefix)}
        calls=[]
        def output(args,**kwargs):
            calls.append(list(map(str,args)))
            if args[1]=='-l':return 'Requesting program interpreter: /lib/ld-linux-x86-64.so.2]'
            return ''
        with patch('cpkt_sdk_consumer.command',side_effect=output):
            self.assertEqual(execute(binary,['argument'],'x86_64-linux-gnu',configured)['status'],'passed')
        library_path=os.pathsep.join(map(str,(prefix/'lib',sysroot/'lib',sysroot/'usr/lib')))
        base=[str(loader),'--library-path',library_path]
        self.assertEqual(calls[1],base+['--list',str(binary)])
        self.assertEqual(calls[2],base+[str(binary),'argument'])

    def test_sqlite_module_uses_generated_target_filename(self):
        from cpkt_sdk_consumer import sqlite_module_path
        build=self.work/'module-build';build.mkdir()
        module=build/'libsqlite_extension_from_package.so'
        module.write_bytes(b'Darwin MODULE suffix fixture')
        (build/'sqlite-module-path.txt').write_text(str(module))
        self.assertEqual(module,sqlite_module_path(build))
        module.unlink()
        with self.assertRaisesRegex(ValueError,'MODULE output missing'):
            sqlite_module_path(build)
        (build/'sqlite-module-path.txt').write_text(str(self.work/'outside.so'))
        with self.assertRaisesRegex(ValueError,'MODULE output missing'):
            sqlite_module_path(build)

    def test_missing_notice_directory_fails_inventory_preflight(self):
        from cpkt_inventory import load,validate_inputs
        data=copy.deepcopy(load(ROOT))
        data['groups'][next(iter(data['groups']))]['package'].setdefault('extra_notices',[]).append('docs/third_party/missing-fixture')
        with self.assertRaisesRegex(RuntimeError,'notice source directory missing'):
            validate_inputs(ROOT,data,next(iter(data['groups'])))

    def test_pkg_config_consumer_rejects_optional_sibling(self):
        from cpkt_inventory import load
        data=copy.deepcopy(load(ROOT))
        data['installed_consumers'][next(iter(data['installed_consumers']))]['pc_extra']=['unowned-sibling-pc']
        (self.work/'cmake').mkdir();(self.work/'cmake/components.json').write_text(json.dumps(data))
        with self.assertRaisesRegex(RuntimeError,'pkg-config sibling dependency'):
            load(self.work)

    def test_source_evidence_rejects_coverage_only_receipt(self):
        import cpkt_source_proof as proof
        source=self.work/'source';graph=source/'build/x86_64-linux-gnu/core/Release';graph.mkdir(parents=True)
        (graph/'CMakeCache.txt').write_text('CPKT_DEPENDENCY_BUILD_JOBS:STRING=8\n')
        receipt={'status':'passed','profile':'native-linux','coverage':['named-but-unexecuted']}
        with patch.object(proof,'read',return_value=receipt):
            self.fails(lambda:proof.reconstruction_evidence(source,'1.2.3'))
        # The production entrypoint must refuse the original working repository.
        with patch.object(proof,'delegated'),patch.object(sys,'argv',['proof','archive','1.2.3',str(ROOT)]):
            with self.assertRaisesRegex(ValueError,'independent extracted source'):proof.main()

    def test_private_module_identity_does_not_hide_missing_library_abi(self):
        prefix=self.work/'module-sdk';lib=prefix/'lib/krb5/plugins/tls';lib.mkdir(parents=True)
        module=lib/'k5tls.so';module.write_bytes(b'actual module fixture')
        configured={'CPKT_TARGET_ID':'x86_64-linux-gnu','CMAKE_READELF':sys.executable}
        with patch('cpkt_packages.command',return_value='Dynamic section without SONAME'):
            self.assertEqual(abi_records(prefix,configured)[module.relative_to(prefix).as_posix()],{'kind':'module','soname':None,'loader_name':module.name})
            (lib/'libpq.so.5').write_bytes(b'ordinary library')
            self.fails(lambda:abi_records(prefix,configured))
        (lib/'libpq.so.5').unlink();module.unlink();module=lib/'k5tls.dylib';module.write_bytes(b'Darwin module')
        configured={'CPKT_TARGET_ID':'arm64-apple-darwin','CMAKE_OTOOL':sys.executable}
        def inspect(args,**kwargs):
            return {'-hv':'MH_MAGIC_64 ARM64 BUNDLE','-D':str(module)+'\n','-L':str(module)+'\n /usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n'}[args[1]]
        with patch('cpkt_packages.command',side_effect=inspect):
            self.assertEqual(abi_records(prefix,configured)[module.relative_to(prefix).as_posix()]['kind'],'module')
            with patch('cpkt_packages.command',return_value='ordinary DYLIB without ID'):
                self.fails(lambda:abi_records(prefix,configured))

    def test_validator_independent_bytes_and_closures(self):
        p,m=self.sdk()
        for groups in (['core'],['core','db'],['core','misc'],['core','db','misc']):self.assertEqual(set(validator.validate(p,groups)),set(groups))
        (p/'misc/payload').write_text('unrelated corrupted optional')
        (p/'share/cpkt/packages/misc.json').write_text('malformed unrelated')
        validator.validate(p,['db'])
        self.fails(lambda:validator.validate(p,['misc']))
        (p/'db/payload').write_text('tamper');self.fails(lambda:validator.validate(p,['db']))
        (p/'db/payload').write_text('db');(p/'db/payload').chmod(0o600);self.fails(lambda:validator.validate(p,['db']))
        (p/'db/payload').chmod(0o644);(p/'db/link').unlink();(p/'db/link').symlink_to('../../outside');self.fails(lambda:validator.validate(p,['db']))
    def test_import_validation_cache_host_predefinition_and_new_operation(self):
        p,m=self.sdk()
        support=p/'share/cpkt/validate-sdk.py'
        support.write_text('import os\nopen(os.environ["CPKT_VALIDATE_COUNTER"],"a").write("call\\n")\n'+(ROOT/'scripts/validate-sdk.py').read_text())
        module=p/'lib/cmake/CpktSDK/Validate.cmake';module.parent.mkdir(parents=True)
        module.write_text((ROOT/'cmake/CpktSDKValidate.cmake').read_text())
        library=p/'lib/libcore.a';library.write_bytes(b'independent library identity fixture')
        catalog=p/'share/cpkt/payload-ownership.json'
        catalog.write_bytes(encoded({'core':['core/*','lib/*','share/cpkt/validate-sdk.py'],'db':['db/*'],'misc':['misc/*']}))
        core=copy.deepcopy(m['core'])
        files={f['path']:f for f in core['files']}
        for path in (support,module,library,catalog):
            path.chmod(0o644);name=path.relative_to(p).as_posix()
            files[name]={'path':name,'type':'file','mode':'0644','sha256':digest(path.read_bytes())}
        core['files']=[files[k] for k in sorted(files)]
        core=self.write_manifest(p,'core',core,True)
        for group in ('db','misc'):
            manifest=copy.deepcopy(m[group]);manifest['requires_core']={k:core[k] for k in ('package_id','release_version','target_id')}
            self.write_manifest(p,group,manifest,True)
        source=self.work/'cmake-source';source.mkdir();counter=self.work/'calls'
        base='cmake_minimum_required(VERSION 3.21)\nproject(independent_import NONE)\ninclude("'+str(module)+'")\n'
        (source/'CMakeLists.txt').write_text(base+'cpkt_sdk_validate(core)\ncpkt_sdk_validate(core)\ncpkt_sdk_validate(db)\ncpkt_sdk_validate(db)\ncpkt_sdk_assert_targets(db)\n')
        env=dict(os.environ,CPKT_VALIDATE_COUNTER=str(counter))
        def configure(name):return subprocess.run(['cmake','-S',str(source),'-B',str(self.work/name)],env=env,capture_output=True,text=True)
        self.assertEqual(configure('one').returncode,0)
        self.assertEqual(counter.read_text().count('call'),2)
        self.assertEqual(configure('two').returncode,0)
        self.assertEqual(counter.read_text().count('call'),4)
        (source/'CMakeLists.txt').write_text(base+'add_library(host STATIC IMPORTED)\nset_target_properties(host PROPERTIES IMPORTED_LOCATION "'+str(self.work/'host/libcore.a')+'")\ncpkt_sdk_validate(core)\ncpkt_sdk_assert_targets(core)\n')
        result=configure('host');self.assertNotEqual(result.returncode,0);self.assertIn('outside validated prefix',' '.join(result.stderr.split()))
        (source/'CMakeLists.txt').write_text(base+'cpkt_sdk_validate(core)\nadd_library(independent_host INTERFACE IMPORTED)\nset_property(TARGET independent_host PROPERTY INTERFACE_LINK_LIBRARIES "$<LINK_ONLY:'+str(self.work/'host/libcore.a')+'>")\ncpkt_sdk_assert_targets(core)\n')
        result=configure('host-interface');self.assertNotEqual(result.returncode,0);self.assertIn('outside validated prefix',' '.join(result.stderr.split()))
        (source/'CMakeLists.txt').write_text(base+'cpkt_sdk_validate(core)\nadd_library(comma_host INTERFACE IMPORTED)\nset_property(TARGET comma_host PROPERTY INTERFACE_LINK_LIBRARIES "$<IF:$<BOOL:0>:'+str(library)+','+str(self.work/'host/libcore.a')+'>")\ncpkt_sdk_assert_targets(core)\n')
        result=configure('host-interface-comma');self.assertNotEqual(result.returncode,0);self.assertIn('outside validated prefix',' '.join(result.stderr.split()))
        for prop in ('IMPORTED_LOCATION_RELEASE','IMPORTED_IMPLIB_RELEASE'):
            (source/'CMakeLists.txt').write_text(base+'cpkt_sdk_validate(core)\nadd_library(configured_host STATIC IMPORTED)\nset_target_properties(configured_host PROPERTIES IMPORTED_CONFIGURATIONS RELEASE '+prop+' "'+str(self.work/'host/libcore.a')+'")\ncpkt_sdk_assert_targets(core)\n')
            result=configure(prop);self.assertNotEqual(result.returncode,0);self.assertIn('outside validated prefix',' '.join(result.stderr.split()))
        (source/'host').mkdir()
        (source/'consumer').mkdir()
        (source/'host/CMakeLists.txt').write_text('add_library(sibling_host STATIC IMPORTED GLOBAL)\n'
            'set_target_properties(sibling_host PROPERTIES IMPORTED_LOCATION "'+str(self.work/'host/libcore.a')+'")\n')
        (source/'consumer/CMakeLists.txt').write_text('include("'+str(module)+'")\ncpkt_sdk_assert_targets(core)\n')
        (source/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(sibling_import NONE)\n'
            'add_subdirectory(host)\nadd_subdirectory(consumer)\n')
        result=configure('sibling-global-host')
        self.assertNotEqual(result.returncode,0)
        self.assertIn('outside validated prefix',' '.join(result.stderr.split()))
        (source/'host/CMakeLists.txt').write_text((source/'host/CMakeLists.txt').read_text().replace('IMPORTED GLOBAL','IMPORTED'))
        result=configure('sibling-local-host')
        self.assertEqual(result.returncode,0,result.stderr)
        # A literal comma is valid in an ordinary installed prefix.
        comma=p.with_name(p.name+',installed');p.rename(comma)
        comma_module=comma/module.relative_to(p);comma_library=comma/library.relative_to(p)
        (source/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(comma_prefix NONE)\ninclude("'+str(comma_module)+'")\nadd_library(own STATIC IMPORTED)\nset_property(TARGET own PROPERTY IMPORTED_LOCATION "'+str(comma_library)+'")\ncpkt_sdk_assert_targets(core)\n')
        result=configure('literal-comma-prefix');self.assertEqual(result.returncode,0,result.stderr)
        comma.rename(p)
        # Every byte/schema is independently consistent for a different target.
        for group in ('core','db','misc'):
            manifest=json.loads((p/'share/cpkt/packages'/f'{group}.json').read_text())
            manifest['target_id']='aarch64-linux-gnu'
            if group!='core':manifest['requires_core']={k:wrongcore[k] for k in ('package_id','release_version','target_id')}
            updated=self.write_manifest(p,group,manifest,True)
            if group=='core':wrongcore=updated
        (source/'CMakeLists.txt').write_text(base+'set(CPKT_TARGET_ID x86_64-linux-gnu)\nset(CMAKE_SYSTEM_NAME Linux)\nset(CMAKE_SYSTEM_PROCESSOR x86_64)\ncpkt_sdk_validate(db)\nadd_library(should_never_import STATIC IMPORTED)\n')
        result=configure('wrong-target');self.assertNotEqual(result.returncode,0);self.assertIn('expected target mismatch',' '.join(result.stderr.split()))
        (source/'CMakeLists.txt').write_text(base+'set(CMAKE_SYSTEM_NAME Linux)\nset(CMAKE_SYSTEM_PROCESSOR x86_64)\ncpkt_sdk_validate(db)\n')
        result=configure('wrong-cpu');self.assertNotEqual(result.returncode,0);self.assertIn('CPU/architecture mismatch',' '.join(result.stderr.split()))
        (p/'core/payload').write_bytes(b'changed after earlier successful configure')
        result=configure('tamper');self.assertNotEqual(result.returncode,0);self.assertIn('validation before imports failed',' '.join(result.stderr.split()))
    def test_preflight_followed_cpp_and_missing_header_before_producer(self):
        from cpkt_inventory import validate_inputs
        root=self.work/'input-root';(root/'tests').mkdir(parents=True)
        (root/'tests/entry.sh').write_text('cat "$repo_root/tests/real.cpp" "$repo_root/tests/real.hpp"\n')
        (root/'tests/real.cpp').write_text('int value;\n')
        data={'components':{},'groups':{},'targets':{},'tests':{'entry':{'group':'core','command_inputs':['tests/entry.sh']}}}
        with self.assertRaisesRegex(RuntimeError,'real.hpp'):validate_inputs(root,data,'core')
        (root/'tests/real.hpp').write_text('extern int value;\n')
        inputs=validate_inputs(root,data,'core');self.assertIn('tests/real.cpp',inputs);self.assertNotIn('tests/real.c',inputs)
    def test_verify_does_not_replace_bad_checksum_or_start_consumers(self):
        import cpkt_packages as packages
        root=self.work/'checksums';root.mkdir()
        shutil.copy2(ROOT/'CMakePresets.json',root/'CMakePresets.json')
        (root/'cmake').mkdir();shutil.copy2(ROOT/'cmake/components.json',root/'cmake/components.json')
        for group,scope in (('core','selected'),('all','binary')):
            names=[packages.archive_name('1.2.3','x86_64-linux-gnu','core')] if group=='core' else packages.artifacts('1.2.3','binary')
            base=root/'build/package-stage/x86_64-linux-gnu/core/archives' if group=='core' else root/'dist'
            manifest=root/'build/verification/x86_64-linux-gnu/core/CHECKSUMS' if group=='core' else root/'build/verification/binary/1.2.3/CHECKSUMS'
            base.mkdir(parents=True,exist_ok=True);manifest.parent.mkdir(parents=True,exist_ok=True)
            for name in names:(base/name).write_bytes(name.encode())
            text=''.join(('a'*64 if i==0 else digest((base/name).read_bytes()))+'  '+name+'\n' for i,name in enumerate(sorted(names)))
            manifest.write_text(text)
            argv=['packages','verify','--group',group,'--scope',scope,'--version','1.2.3']+(['--preset','x86_64-linux-gnu-release'] if group=='core' else [])
            with patch.object(packages,'ROOT',root),patch.object(packages,'delegated'),patch.object(packages,'verify_selected') as consumer,patch.object(packages,'combinations') as combinations,patch.object(sys,'argv',argv),patch.dict(os.environ,CPKT_OPERATION_FD='fixture'):
                with self.assertRaisesRegex(ValueError,'checksum mismatch'):packages.main()
                consumer.assert_not_called();combinations.assert_not_called()
            self.assertEqual(manifest.read_text(),text)
    def test_smoke_zip_preflight_paths_modes_and_links(self):
        from cpkt_darwin import extract_smoke
        for case in ('good','escape','ancestor','duplicate','outside','special'):
            archive=self.work/(case+'.zip');destination=self.work/case
            with zipfile.ZipFile(archive,'w') as stream:
                def member(name,body,mode):
                    item=zipfile.ZipInfo(name);item.create_system=3;item.external_attr=mode<<16;stream.writestr(item,body)
                if case=='outside':member('other/file',b'bad',0o100644)
                elif case=='special':member('darwin-smoke-test/fifo',b'',0o010644)
                else:
                    member('darwin-smoke-test/bin/program',b'executable',0o100755)
                    if case=='duplicate':
                        with self.assertWarnsRegex(UserWarning,'Duplicate name'):
                            member('darwin-smoke-test/bin/program',b'again',0o100755)
                    elif case=='ancestor':
                        member('darwin-smoke-test/link',b'bin',0o120777);member('darwin-smoke-test/link/child',b'bad',0o100644)
                    else:member('darwin-smoke-test/bin/link',b'../../../outside' if case=='escape' else b'program',0o120777)
            if case=='good':
                extract_smoke(archive,destination)
                self.assertEqual((destination/'darwin-smoke-test/bin/program').stat().st_mode & 0o777,0o755)
                self.assertTrue((destination/'darwin-smoke-test/bin/link').is_symlink())
            else:
                self.fails(lambda:extract_smoke(archive,destination));self.assertFalse(destination.exists())
    def test_validator_schema_collisions_missing_extra_identity(self):
        for name in ('unknown','schema','noncanonical','self','duplicate','core-id','target','version','missing','extra','symlink-ancestor'):
            with self.subTest(name=name):
                sub=self.work/name;sub.mkdir();old=self.work;self.work=sub;p,m=self.sdk();self.work=old
                value=copy.deepcopy(m['db'])
                if name=='unknown':value['unknown']=True
                elif name=='schema':value['schema_version']=2
                elif name=='noncanonical':
                    path=p/'share/cpkt/packages/db.json';path.write_text(json.dumps(value,indent=2));self.fails(lambda:validator.validate(p,['db']));continue
                elif name=='self':value['files'].append({'path':'share/cpkt/packages/db.json','type':'file','mode':'0644','sha256':'b'*64})
                elif name=='duplicate':value['files'].append(value['files'][0])
                elif name=='core-id':value['requires_core']['package_id']='b'*64
                elif name=='target':value['target_id']='armhf-linux-gnu'
                elif name=='version':value['requires_core']['release_version']='1.2.4'
                elif name=='missing':(p/'db/payload').unlink()
                elif name=='extra':(p/'unexpected').write_text('extra')
                elif name=='symlink-ancestor':shutil.rmtree(p/'db');(p/'db').symlink_to('core')
                if name not in ('missing','extra','symlink-ancestor'):self.write_manifest(p,'db',value,True)
                self.fails(lambda:validator.validate(p,['db']))
    def test_archive_collision_escape_and_owner(self):
        for path,link,uid in [('sdk/file',None,0),('sdk/../../escape',None,0),('sdk/link','../../escape',0),('sdk/file',None,1)]:
            archive=self.work/f'archive-{uid}-{len(list(self.work.glob("archive*")))}.tgz'
            with tarfile.open(archive,'w:gz') as stream:
                item=tarfile.TarInfo(path);item.uid=uid
                if link:item.type=tarfile.SYMTYPE;item.linkname=link;stream.addfile(item)
                else:item.size=1;stream.addfile(item,io.BytesIO(b'x'))
            if uid or link or '..' in path:self.fails(lambda:safe_extract(archive,self.work/'extract','sdk'))
            else:
                safe_extract(archive,self.work/'extract','sdk');self.fails(lambda:safe_extract(archive,self.work/'extract','sdk'))
    def test_exact_checksum_scopes(self):
        self.assertEqual(len(artifacts('1.2.3','binary')),8);self.assertEqual(len(artifacts('1.2.3','release')),9)
        base=self.work/'dist';base.mkdir()
        for name in artifacts('1.2.3','binary'):(base/name).write_text(name)
        manifest=self.work/'verification/CHECKSUMS';manifest.parent.mkdir()
        manifest.write_text(''.join(digest((base/name).read_bytes())+'  '+name+'\n' for name in artifacts('1.2.3','binary')))
        check_snapshot(manifest,base,'1.2.3','binary')
        for name in ('unlisted.tar.gz','CHECKSUMS'):
            extra=base/name;extra.write_bytes(b'unlisted')
            with self.assertRaisesRegex(ValueError,'unexpected distribution payloads'):
                check_snapshot(manifest,base,'1.2.3','binary')
            extra.unlink()
        manifest.write_text(manifest.read_text().splitlines()[0]+'\n');self.fails(lambda:check_snapshot(manifest,base,'1.2.3','binary'))
    def test_binary_verify_rejects_extra_payload_before_consumers(self):
        import cpkt_packages as packages
        root=self.work/'extra-payload';dist=root/'dist';dist.mkdir(parents=True)
        (root/'cmake').mkdir();shutil.copy2(ROOT/'cmake/components.json',root/'cmake/components.json')
        ver='1.2.3';names=artifacts(ver,'binary')
        for name in names:(dist/name).write_bytes(name.encode())
        manifest=root/'build/verification/binary'/ver/'CHECKSUMS';manifest.parent.mkdir(parents=True)
        manifest.write_text(''.join(digest((dist/name).read_bytes())+'  '+name+'\n' for name in names))
        (dist/'stale.tar.gz').write_bytes(b'unlisted payload')
        with patch.object(packages,'ROOT',root),patch.object(packages,'delegated'),patch.object(packages,'privacy') as privacy,patch.object(packages,'combinations') as consumers,patch.object(sys,'argv',['packages','verify','--group','all','--scope','binary','--version',ver]),patch.dict(os.environ,{'CPKT_OPERATION_FD':'fixture','CPKT_OPERATION_RUN':'fixture'}):
            with self.assertRaisesRegex(ValueError,'unexpected distribution payloads'):
                packages.main()
        privacy.assert_not_called();consumers.assert_not_called()
        self.assertFalse((root/'build/verification/binary'/ver/'proof.json').exists())
    def test_failed_aggregate_verification_revokes_prior_publication_proof(self):
        import cpkt_packages as packages
        root=self.work/'stale-proof';root.mkdir()
        (root/'cmake').mkdir();shutil.copy2(ROOT/'cmake/components.json',root/'cmake/components.json')
        ver='1.2.3'
        for scope,action,failure in (('binary','verify','checksum mismatch'),
                                     ('release','verify','consumer failed'),
                                     ('release','checksums','source proof failed')):
            proof=root/'build/verification'/scope/ver/'proof.json'
            proof.parent.mkdir(parents=True,exist_ok=True)
            proof.write_text('{"kind":"artifact-'+scope+'","status":"passed"}')
            with patch.object(packages,'ROOT',root),patch.object(packages,'delegated'),\
                 patch.object(packages,'check_snapshot' if action=='verify' else 'checksum_snapshot',
                              side_effect=ValueError(failure)),\
                 patch.object(sys,'argv',['packages',action,'--group','all','--scope',scope,'--version',ver]),\
                 patch.dict(os.environ,{'CPKT_OPERATION_FD':'fixture','CPKT_OPERATION_RUN':'fixture'}):
                with self.assertRaisesRegex(ValueError,failure):packages.main()
            self.assertFalse(proof.exists(),scope+' '+action+' retained stale publication proof')
    def test_release_rejects_narrowing_before_clean(self):
        before=(ROOT/'build/control/operation.lock').stat().st_ino if (ROOT/'build/control/operation.lock').exists() else None
        for args in (['release','GROUP=db'],['release','PRESET=debug'],['release','SCOPE=binary'],['package','GROUP=db'],['fuzz','GROUP=db'],['clean','GROUP=bogus']):
            result=subprocess.run(['make','--no-print-directory',*args],cwd=ROOT,text=True,capture_output=True)
            self.assertNotEqual(result.returncode,0,args)
        after=(ROOT/'build/control/operation.lock').stat().st_ino if (ROOT/'build/control/operation.lock').exists() else None
        self.assertEqual(before,after)

    def test_native_darwin_debug_uses_the_declared_preset(self):
        import cpkt_lifecycle as lifecycle
        for action in ('debug','build-debug','clangd-surface'):
            with patch.object(sys,'argv',['lifecycle',action,'--group','core','--preset','arm64-apple-darwin-debug']),patch.dict(os.environ,{'CPKT_OPERATION_FD':'fixture'}),patch.object(lifecycle,'delegated'),patch.object(lifecycle,'backend') as backend,patch.object(lifecycle,'command'):
                lifecycle.main()
            backend.assert_called_once_with('test' if action=='debug' else 'build','core','arm64-apple-darwin-debug')

    def test_shell_defaults_preserve_matrix_and_selected_scope(self):
        tools=self.work/'dispatch-tools';tools.mkdir();record=self.work/'dispatch.jsonl'
        executable=tools/'python3'
        executable.write_text('#!'+sys.executable+'\nimport json,os,sys,subprocess\nfrom pathlib import Path\nargs=sys.argv[1:]\nwith open(os.environ["CPKT_DISPATCH_RECORD"],"a") as stream:stream.write(json.dumps(args)+"\\n")\nif Path(args[0]).name=="cpkt_operation.py":sys.exit(subprocess.run(args[args.index("--")+1:]).returncode)\n')
        executable.chmod(0o755)
        base={k:v for k,v in os.environ.items() if k not in ('GROUP','PRESET') and not k.startswith('CPKT_OPERATION_')}
        base.update(PATH=str(tools)+os.pathsep+base['PATH'],CPKT_DISPATCH_RECORD=str(record))
        targets=[t for t in json.loads((ROOT/'cmake/components.json').read_text())['package_targets'] if '-linux-' in t]
        for script,action in (('build.sh','build'),('test.sh','test')):
            for env,arguments,presets in ((base,[],[t+'-release' for t in targets]),(dict(base,GROUP='core'),[],['debug']),(dict(base,GROUP='db',PRESET='armhf-linux-musl-release'),[],['armhf-linux-musl-release']),(dict(base,GROUP='core'),['release'],['release'])):
                record.unlink(missing_ok=True)
                result=subprocess.run(['bash',str(ROOT/'scripts'/script),*arguments],env=env,text=True,capture_output=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                calls=[json.loads(line) for line in record.read_text().splitlines()]
                dispatched=[args for args in calls if Path(args[0]).name=='group-build.py']
                self.assertEqual([args[args.index('--preset')+1] for args in dispatched],presets)
                self.assertTrue(all(args[1]==action for args in dispatched))
                self.assertEqual(sum(Path(args[0]).name=='cpkt_operation.py' for args in calls),int(len(presets)==6))

    def test_requested_flags_expand_typed_macros_and_preset_environment(self):
        module_spec=importlib.util.spec_from_file_location('producer_flag_fixture',ROOT/'scripts/group-build.py')
        module=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(module)
        definitions=json.loads((ROOT/'CMakePresets.json').read_text())
        base=next(p for p in definitions['configurePresets'] if p['name']=='base')
        base['environment']={'CPKT_FLAG_VALUE':'$penv{CPKT_PARENT_FLAG}','CFLAGS':'-DPRESET_ENVIRONMENT=1'}
        debug=next(p for p in definitions['configurePresets'] if p['name']=='debug')
        debug['cacheVariables']['CMAKE_C_FLAGS']={'type':'STRING','value':'-DVALUE=$env{CPKT_FLAG_VALUE} -DPARENT=$penv{CPKT_FLAG_VALUE} -DROOT=${sourceDirName} -DLITERAL=${dollar}{sourceDir}'}
        (self.work/'CMakePresets.json').write_text(json.dumps(definitions))
        with patch.object(module,'ROOT',self.work),patch.dict(os.environ,{'CPKT_PARENT_FLAG':'chosen','CPKT_FLAG_VALUE':'parent'}):
            self.assertEqual(module.producer_flags('debug','core')['CMAKE_C_FLAGS'],'-DVALUE=chosen -DPARENT=parent -DROOT='+self.work.name+' -DLITERAL=${sourceDir}')
            del debug['cacheVariables']['CMAKE_C_FLAGS']
            (self.work/'CMakePresets.json').write_text(json.dumps(definitions))
            self.assertEqual(module.producer_flags('debug','core')['CMAKE_C_FLAGS'],'-DPRESET_ENVIRONMENT=1')
            consumer=self.work/'build/x86_64-linux-gnu/core/Debug/CMakeCache.txt';consumer.parent.mkdir(parents=True)
            consumer.write_text('CMAKE_C_FLAGS:STRING=-DCACHED_FLAGS=1\n')
            self.assertEqual(module.producer_flags('debug','core')['CMAKE_C_FLAGS'],'-DCACHED_FLAGS=1')

    def test_producer_preset_switch_resets_linker_flags(self):
        module_spec=importlib.util.spec_from_file_location('producer_linker_flag_fixture',ROOT/'scripts/group-build.py')
        module=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(module)
        definitions=json.loads((ROOT/'CMakePresets.json').read_text())
        release=next(p for p in definitions['configurePresets'] if p['name']=='release')
        release.setdefault('cacheVariables',{})
        release['cacheVariables'].pop('CMAKE_SHARED_LINKER_FLAGS',None)
        release['cacheVariables'].pop('CMAKE_EXE_LINKER_FLAGS',None)
        release['cacheVariables'].pop('CMAKE_MODULE_LINKER_FLAGS',None)
        (self.work/'CMakePresets.json').write_text(json.dumps(definitions))
        producer=self.work/'.cache/deps-build/x86_64-linux-gnu/core/producer'
        producer.mkdir(parents=True)
        (producer/'CMakeCache.txt').write_text('CMAKE_SHARED_LINKER_FLAGS:STRING=-Wl,old\n'
            'CMAKE_EXE_LINKER_FLAGS:STRING=-Wl,old\n'
            'CMAKE_MODULE_LINKER_FLAGS:STRING=-Wl,old\n')
        with patch.object(module,'ROOT',self.work),patch.dict(os.environ,{'LDFLAGS':''}):
            flags=module.producer_flags('release','core')
            for key in ('CMAKE_SHARED_LINKER_FLAGS','CMAKE_EXE_LINKER_FLAGS',
                    'CMAKE_MODULE_LINKER_FLAGS','CMAKE_STATIC_LINKER_FLAGS'):
                self.assertEqual('',flags[key])
            self.assertTrue(module.requested_producer_change('release','core',producer))

    def test_release_production_enforces_same_run_source_evidence(self):
        import cpkt_lifecycle as lifecycle
        calls=[]
        with patch.dict(os.environ,{},clear=False),patch.object(lifecycle,'command',side_effect=lambda args,group:calls.append((list(map(str,args)),os.environ.get('CPKT_RELEASE_PRODUCTION')))),patch.object(lifecycle,'backend'),patch.object(lifecycle.subprocess,'check_output',return_value='1.2.3\n'),patch.object(lifecycle,'child_delegation',side_effect=lambda root,group:nullcontext((dict(os.environ),()))),patch.object(lifecycle.subprocess,'run') as direct:
            os.environ.pop('CPKT_RELEASE_PRODUCTION',None)
            lifecycle.perform('release','all','debug',False,'',False,'')
            self.assertTrue(all(call.kwargs['env']['CPKT_RELEASE_PRODUCTION']=='1' for call in direct.call_args_list))
        self.assertTrue(calls)
        self.assertTrue(all(flag=='1' for _,flag in calls))
        for action in ('checksums','verify'):
            self.assertTrue(any(action in args and 'release' in args for args,_ in calls))

    def test_standalone_release_verification_accepts_prior_matching_source_proof(self):
        import cpkt_packages as packages
        root=self.work/'release-verification';dist=root/'dist';dist.mkdir(parents=True)
        (root/'cmake').mkdir();shutil.copy2(ROOT/'cmake/components.json',root/'cmake/components.json')
        ver='1.2.3'
        for name in artifacts(ver,'release'):(dist/name).write_bytes(b'fixture '+name.encode())
        source=dist/f'cpkt-{ver}.tar.gz'
        proof={'schema_version':1,'status':'passed','kind':'source-reconstruction','archive_sha256':digest(source.read_bytes()),'release_version':ver,'run':'prior-run','coverage':{g:{'outputs':{'fixture':'compiled'},'tests':['executed'],'consumer_cases':[{'status':'passed','runtime':'native'}]} for g in ('core',)},'composition':{'status':'passed','combinations':[{'order':order,'consumer_cases':[{'status':'passed','runtime':'native'}]} for order in [['core']] ]}}
        path=root/'build/verification/source'/ver/'proof.json';path.parent.mkdir(parents=True);path.write_text(json.dumps(proof))
        with patch.object(packages,'ROOT',root),patch.object(packages,'delegated'),patch.object(packages,'privacy'),patch.object(packages,'combinations') as combinations,patch.dict(os.environ,{'CPKT_OPERATION_FD':'fixture','CPKT_OPERATION_RUN':'new-run'}):
            os.environ.pop('CPKT_RELEASE_PRODUCTION',None)
            for action in ('checksums','verify'):
                with patch.object(sys,'argv',['packages',action,'--group','all','--scope','release','--version',ver]):packages.main()
            self.assertEqual(combinations.call_count,7)
            self.assertEqual(json.loads(path.read_text())['run'],'prior-run')
            artifact=root/'build/verification/release'/ver/'proof.json'
            self.assertEqual(json.loads(artifact.read_text())['run'],'new-run')
            combinations.reset_mock();os.environ['CPKT_RELEASE_PRODUCTION']='1'
            for action in ('checksums','verify'):
                with patch.object(sys,'argv',['packages',action,'--group','all','--scope','release','--version',ver]),self.assertRaisesRegex(ValueError,'current successful independent source'):
                    packages.main()
            combinations.assert_not_called()
            os.environ.pop('CPKT_RELEASE_PRODUCTION',None)
            source.write_bytes(b'corrupted source archive')
            with patch.object(sys,'argv',['packages','verify','--group','all','--scope','release','--version',ver]),self.assertRaisesRegex(ValueError,'checksum mismatch'):
                packages.main()
    def test_first_parent_cmake_priority(self):
        (self.work/'CMakePresets.json').write_text(json.dumps({'version':3,'configurePresets':[{'name':'first','hidden':True,'cacheVariables':{'CPKT_TARGET_ARCH':'x86_64','CPKT_TARGET_OS':'linux','CPKT_TARGET_LIBC':'gnu','CMAKE_BUILD_TYPE':'Release','FIXTURE':'first'}},{'name':'second','hidden':True,'cacheVariables':{'CPKT_TARGET_ARCH':'armhf','CPKT_TARGET_OS':'linux','CPKT_TARGET_LIBC':'musl','CMAKE_BUILD_TYPE':'Debug','FIXTURE':'second'}},{'name':'selected','inherits':['first','second'],'generator':'Ninja','binaryDir':str(self.work/'binary')}]}))
        (self.work/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(priority NONE)\nfile(WRITE "${CMAKE_BINARY_DIR}/answer" "${FIXTURE};${CPKT_TARGET_ARCH};${CMAKE_BUILD_TYPE};${CPKT_TARGET_OS};${CPKT_TARGET_LIBC}")\n')
        subprocess.run(['cmake','--preset','selected'],cwd=self.work,check=True,stdout=subprocess.DEVNULL)
        info,target,mode=preset_info(self.work,'selected');self.assertEqual((self.work/'binary/answer').read_text(),'first;x86_64;Release;linux;gnu');self.assertEqual((target,mode),('x86_64-linux-gnu','Release'))
    def tag_repo(self):
        repo=self.work/'repo';repo.mkdir();subprocess.run(['git','init','-q','-b','feature',repo],check=True)
        subprocess.run(['git','-C',repo,'-c','user.name=Fixture','-c','user.email=fixture@example.invalid','commit','--allow-empty','-qm','test: fixture'],check=True)
        return repo
    def test_reserved_tag_owned_unowned_moved_annotated_interrupted(self):
        repo=self.tag_repo();oid=git(repo,'rev-parse','HEAD');ref='refs/tags/v99.99.99'
        create(repo);recover(repo);self.assertFalse(git(repo,'show-ref','--verify',ref,check=False))
        git(repo,'update-ref',ref,oid);self.fails(lambda:recover(repo));self.assertEqual(git(repo,'rev-parse',ref),oid);git(repo,'update-ref','-d',ref,oid)
        create(repo);subprocess.run(['git','-C',repo,'-c','user.name=Fixture','-c','user.email=fixture@example.invalid','commit','--allow-empty','-qm','test: moved'],check=True);moved=git(repo,'rev-parse','HEAD');git(repo,'update-ref',ref,moved,oid);self.fails(lambda:recover(repo));self.assertEqual(git(repo,'rev-parse',ref),moved);git(repo,'update-ref',ref,oid,moved);self.fails(lambda:recover(repo));git(repo,'update-ref','-d',ref,oid);(repo/'build/control/reserved-tag.json').unlink()
        create(repo);record=repo/'build/control/reserved-tag.json';value=json.loads(record.read_text());value['state']='prepared';record.write_text(json.dumps(value));recover(repo)
        subprocess.run(['git','-C',repo,'-c','user.name=Fixture','-c','user.email=fixture@example.invalid','tag','-a','v99.99.99','-m','unowned annotated'],check=True);self.fails(lambda:recover(repo));self.assertEqual(git(repo,'cat-file','-t',ref),'tag')
    def handoff(self):
        names=artifacts('1.2.3','release')+['cpkt-1.2.3-CHECKSUMS']
        return {'schema_version':1,'repository':REPOSITORY,'producer_commit':'a'*40,'tag':'v1.2.3','version':'1.2.3','manifest_sha256':'b'*64,'draft_id':99,'assets':{name:{'id':n+1,'size':3,'sha256':'b'*64} for n,name in enumerate(names)}}
    def test_authenticated_draft_read_identity(self):
        value=self.handoff();validate_handoff(value)
        class API:
            def json(self,path):
                if '/git/ref/' in path:return {'object':{'type':'commit','sha':value['producer_commit']}}
                if '/assets?' in path:return [{'name':n,'id':a['id'],'size':a['size'],'state':'uploaded','digest':'sha256:'+a['sha256']} for n,a in value['assets'].items()]
                return {'draft':True,'tag_name':value['tag'],'id':value['draft_id']}
        draft_matches(API(),value)
        for field in ('producer_commit','tag','manifest_sha256','assets'):
            broken=copy.deepcopy(value)
            if field=='assets':broken[field].pop(next(iter(broken[field])))
            else:broken[field]='wrong'
            self.fails(lambda:validate_handoff(broken))
        class Wrong(API):
            def json(self,path):
                result=super().json(path)
                if isinstance(result,list):result[0]['digest']='sha256:'+'c'*64
                return result
        self.fails(lambda:draft_matches(Wrong(),value))
    def test_complete_handoff_cache_hits_zero_requests_and_one_corrupt_miss(self):
        value=self.handoff();payloads={name:name.encode() for name in value['assets'] if not name.endswith('-CHECKSUMS')}
        manifest_name='cpkt-1.2.3-CHECKSUMS'
        payloads[manifest_name]=''.join(digest(payloads[name])+'  '+name+'\n' for name in sorted(payloads)).encode()
        for name,asset in value['assets'].items():asset.update(size=len(payloads[name]),sha256=digest(payloads[name]))
        value['manifest_sha256']=value['assets'][manifest_name]['sha256']
        shared=self.work/'shared';shared.mkdir();fixture_repo=self.work/'cache-consumer';(fixture_repo/'build/control').mkdir(parents=True);local=fixture_repo/'.cache';local.mkdir();destination=fixture_repo/'build/download'
        selected=[name for name in value['assets'] if '-arm64-apple-darwin' in name or name.endswith('-CHECKSUMS')]
        for name in selected:(shared/('renamed-'+str(value['assets'][name]['id']))).write_bytes(payloads[name])
        class API:
            def __init__(api):api.queries=[];api.opens=[]
            def json(api,path):
                api.queries.append(path)
                if '/git/ref/' in path:return {'object':{'type':'commit','sha':value['producer_commit']}}
                if '/assets?' in path:return [dict(name=name,id=a['id'],size=a['size'],state='uploaded',digest='sha256:'+a['sha256']) for name,a in value['assets'].items()]
                return {'draft':True,'id':value['draft_id'],'tag_name':value['tag']}
            def open(api,url,**kwargs):
                api.opens.append(url);id=int(url.rsplit('/',1)[1])
                return io.BytesIO(next(payloads[n] for n,a in value['assets'].items() if a['id']==id))
        api=API();download_handoff(api,value,shared,destination)
        self.assertEqual(api.queries,[]);self.assertEqual(api.opens,[])
        name=next(n for n in selected if n.endswith('.tar.gz'))
        (shared/('renamed-'+str(value['assets'][name]['id']))).write_bytes(b'corrupt')
        download_handoff(api,value,shared,destination)
        self.assertEqual(len(api.queries),3);self.assertEqual(len(api.opens),1)
        self.assertTrue(api.opens[0].endswith('/'+str(value['assets'][name]['id'])))
        backend_spec=importlib.util.spec_from_file_location('cache_fixture_backend',ROOT/'scripts/group-build.py')
        backend=importlib.util.module_from_spec(backend_spec);backend_spec.loader.exec_module(backend)
        with patch.object(backend,'ROOT',fixture_repo):backend.clean('all')
        self.assertFalse(destination.exists());self.assertFalse(local.exists())
        download_handoff(api,value,shared,destination)
        self.assertEqual(len(api.queries),3);self.assertEqual(len(api.opens),1)
        self.assertFalse(local.exists())
        for name in selected:self.assertEqual((destination/name).read_bytes(),payloads[name])
        dispatch_identity(value,value['producer_commit'],{'GITHUB_ACTIONS':'true','GITHUB_REF_TYPE':'tag','GITHUB_REF_NAME':value['tag'],'GITHUB_SHA':value['producer_commit']})
        self.fails(lambda:dispatch_identity(value,'b'*40,{}))
        self.fails(lambda:dispatch_identity(value,value['producer_commit'],{'GITHUB_ACTIONS':'true','GITHUB_REF_TYPE':'branch'}))
        for token in ('','bad\ncredential','bad credential','nonascii-\u2603'):
            with patch.dict(os.environ,{'CPKT_DRAFT_TOKEN':'','GH_TOKEN':''}):self.fails(lambda:GitHub(token=token))
    def test_digest_hit_zero_network_and_corrupt_miss(self):
        payload=b'fixture';asset={'id':37,'size':len(payload),'sha256':digest(payload)}
        class API:
            calls=0
            def open(self,*args,**kwargs):self.calls+=1;return io.BytesIO(payload)
        api=API();first=acquire(api,asset,self.work);self.assertEqual(api.calls,1);self.assertEqual(acquire(api,asset,self.work),first);self.assertEqual(api.calls,1)
        first.write_bytes(b'corrupt');acquire(api,asset,self.work);self.assertEqual(api.calls,2)
        class Bad(API):
            def open(self,*args,**kwargs):return io.BytesIO(b'tamper')
        first.unlink();self.fails(lambda:acquire(Bad(),asset,self.work));self.assertFalse(first.exists())
    def test_credentials_only_official_redirects(self):
        class Opener:
            requests=[]
            def open(self,request,timeout):
                self.requests.append(request)
                if len(self.requests)==1:raise urllib.error.HTTPError(request.full_url,302,'redirect',{'Location':'https://release-assets.githubusercontent.com/path?secret=query'},None)
                return io.BytesIO(b'bytes')
        opener=Opener();api=GitHub('private-token',opener);api.open('https://api.github.com/repos/example/releases/assets/1',accept='application/octet-stream')
        self.assertEqual(opener.requests[0].get_header('Authorization'),'Bearer private-token');self.assertIsNone(opener.requests[1].get_header('Authorization'))
        for url in ('http://api.github.com/a','https://evil.example/a','https://user@api.github.com/a'):self.fails(lambda:api.open(url))

    def test_handoff_duplicate_keys_and_control_plane_preflight(self):
        import cpkt_github_handoff as handoff
        value=self.handoff();path=self.work/'handoff.json'
        path.write_bytes(encoded(value))
        api=unittest.mock.Mock()
        args=['handoff','preflight','--handoff',str(path)]
        with patch.object(sys,'argv',args),patch.dict(os.environ,CPKT_OPERATION_FD='fixture'),patch.object(handoff,'delegated'),patch.object(handoff,'GitHub',return_value=api),patch.object(handoff,'dispatch_identity'),patch('cpkt_reserved_tag.git',return_value=value['producer_commit']),patch.object(handoff,'draft_matches') as remote:
            handoff.main();remote.assert_called_once_with(api,value)
        path.write_bytes(encoded(value)[:-1]+b',"draft_id":100}')
        with patch.object(sys,'argv',args),patch.dict(os.environ,CPKT_OPERATION_FD='fixture'),patch.object(handoff,'delegated'),patch.object(handoff,'GitHub',return_value=api):
            with self.assertRaisesRegex(ValueError,'duplicate JSON key'):handoff.main()

    def test_cache_symlinks_cleanup_and_locked_revalidation(self):
        import cpkt_github_handoff as handoff
        payload=b'locked fixture';asset={'id':1,'size':len(payload),'sha256':digest(payload)}
        root=self.work/'shared';root.mkdir();outside=self.work/'outside';outside.write_bytes(payload)
        (root/'renamed-link').symlink_to(outside)
        api=unittest.mock.Mock();api.open.return_value=io.BytesIO(b'tampered')
        self.fails(lambda:acquire(api,asset,root));self.assertFalse(list(root.glob('archives/sha256/*/.part-*')))
        (self.work/'linked-root').symlink_to(root,target_is_directory=True)
        self.fails(lambda:acquire(api,asset,self.work/'linked-root'))
        real=handoff.fcntl.lockf
        def lock(fd,mode):
            real(fd,mode)
            (root/'published-while-waiting').write_bytes(payload)
        api.reset_mock()
        with patch.object(handoff.fcntl,'lockf',side_effect=lock):
            hit=acquire(api,asset,root,lambda: self.fail('metadata request on locked digest hit'))
        self.assertEqual(hit.name,'published-while-waiting');api.open.assert_not_called()

    def test_archive_digest_lock_interoperates_with_cmake_process(self):
        payload=b'cross-process digest lock';asset={'id':1,'size':len(payload),'sha256':digest(payload)}
        root=self.work/'shared';script=self.work/'digest-lock.cmake'
        script.write_text('file(LOCK "'+str(root/'locks'/(asset['sha256']+'.lock'))+'" GUARD PROCESS TIMEOUT 0 RESULT_VARIABLE status)\nif(NOT status STREQUAL "0")\nmessage(FATAL_ERROR "digest lock held")\nendif()\n')
        api=unittest.mock.Mock();api.open.return_value=io.BytesIO(payload)
        def locked():
            result=subprocess.run(['cmake','-P',str(script)],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0);self.assertIn('digest lock held',result.stderr)
        acquire(api,asset,root,locked)
        subprocess.run(['cmake','-P',str(script)],check=True,capture_output=True)

    def test_reserved_tag_same_oid_foreign_and_crash_and_symlinks(self):
        import cpkt_reserved_tag as tags
        repo=self.tag_repo();oid=git(repo,'rev-parse','HEAD');ref=tags.REF
        for state in ('prepared','created'):
            create(repo);record=repo/'build/control/reserved-tag.json'
            data=json.loads(record.read_text());data['state']=state;record.write_text(json.dumps(data))
            git(repo,'update-ref','-d',ref,oid);git(repo,'update-ref','--create-reflog','-m','foreign same HEAD',ref,oid,'0'*len(oid))
            self.fails(lambda:recover(repo));self.assertEqual(git(repo,'rev-parse',ref),oid)
            git(repo,'update-ref','-d',ref,oid);record.unlink()
        original=tags.persist;count=0
        def interrupted(path,data):
            nonlocal count
            count+=1
            if count==2:raise OSError('interrupted after successful exclusive creation')
            original(path,data)
        with patch.object(tags,'persist',side_effect=interrupted):self.fails(lambda:create(repo))
        self.assertEqual(json.loads(record.read_text())['state'],'prepared')
        # Global clean removes compiled state but explicitly retains control/ref ownership.
        spec=importlib.util.spec_from_file_location('audit_group_build',ROOT/'scripts/group-build.py');backend=importlib.util.module_from_spec(spec);spec.loader.exec_module(backend)
        (repo/'.cache').mkdir();(repo/'build/scratch').mkdir()
        with patch.object(backend,'ROOT',repo):backend.clean('all')
        self.assertTrue(record.is_file());recover(repo);self.assertFalse(git(repo,'show-ref','--verify',ref,check=False))
        shutil.rmtree(repo/'build');(repo/'build').symlink_to(self.work/'absent',target_is_directory=True)
        self.fails(lambda:recover(repo));self.fails(lambda:create(repo));self.assertFalse((self.work/'absent').exists())

    def test_failed_exclusive_tag_create_does_not_own_foreign_same_head(self):
        import cpkt_reserved_tag as tags
        repo=self.tag_repo();oid=git(repo,'rev-parse','HEAD');original=tags.git
        def foreign_wins(root,*args,**kwargs):
            if args[:2]==('update-ref','--create-reflog'):
                original(root,'update-ref','--create-reflog','-m','foreign exclusive winner',tags.REF,oid,'0'*len(oid))
            return original(root,*args,**kwargs)
        with patch.object(tags,'git',side_effect=foreign_wins):self.fails(lambda:create(repo))
        record=json.loads((repo/'build/control/reserved-tag.json').read_text())
        self.assertEqual(record['state'],'prepared');self.fails(lambda:recover(repo))
        self.assertEqual(git(repo,'rev-parse',tags.REF),oid)

    def test_cross_manifest_claim_is_reserved_metadata(self):
        prefix,manifests=self.sdk();item=copy.deepcopy(manifests['db'])
        name='share/cpkt/packages/core.json'
        item['files'].append({'path':name,'type':'file','mode':'0644','sha256':digest((prefix/name).read_bytes())})
        item['files'].sort(key=lambda entry:entry['path']);self.write_manifest(prefix,'db',item,True)
        with self.assertRaisesRegex(ValueError,'collision/self-inclusion'):validator.validate(prefix,['core','db'])

    def test_source_effective_lower_jobs_cache_and_generator_executable(self):
        root=self.work/'source-parent';root.mkdir();shutil.copy2(ROOT/'CMakePresets.json',root/'CMakePresets.json')
        cache=root/'build/x86_64-linux-gnu/core/Release/CMakeCache.txt';cache.parent.mkdir(parents=True)
        custom=str(self.work/'declared shared cache')
        cache.write_text('CPKT_DEPENDENCY_BUILD_JOBS:STRING=3\nCPKT_DEPENDENCY_CACHE:PATH='+custom+'\nCMAKE_GENERATOR:INTERNAL=Unix Makefiles\n')
        env={k:v for k,v in os.environ.items() if k not in ('CPKT_DEPENDENCY_BUILD_JOBS','CMAKE_BUILD_PARALLEL_LEVEL','CPKT_DEPENDENCY_CACHE','PRESET')}
        env['CPKT_PRESET']='x86_64-linux-gnu-release'
        args=[sys.executable,str(ROOT/'scripts/cpkt_source_reconstruct.py'),'--environment-from',str(root)]
        result=subprocess.run(args,env=env,capture_output=True,text=True,check=True);values=json.loads(result.stdout)
        self.assertEqual(values['CPKT_DEPENDENCY_BUILD_JOBS'],'3');self.assertEqual(values['CPKT_DEPENDENCY_CACHE'],custom);self.assertEqual(values['CPKT_SOURCE_GENERATOR'],'Unix Makefiles')
        env['CPKT_DEPENDENCY_BUILD_JOBS']='1';result=subprocess.run(args,env=env,capture_output=True,text=True,check=True)
        self.assertEqual(json.loads(result.stdout)['CPKT_DEPENDENCY_BUILD_JOBS'],'1')
        from cpkt_source_reconstruct import reconstruction_environment
        fresh=self.work/'fresh-source';fresh.mkdir();shutil.copy2(ROOT/'CMakePresets.json',fresh/'CMakePresets.json')
        self.assertEqual(reconstruction_environment(fresh,values)['CPKT_SOURCE_GENERATOR'],'Unix Makefiles')
        env['CPKT_DEPENDENCY_BUILD_JOBS']='4';self.assertNotEqual(subprocess.run(args,env=env,capture_output=True).returncode,0)
        env['CPKT_DEPENDENCY_BUILD_JOBS']='9';self.assertNotEqual(subprocess.run(args,env=env,capture_output=True).returncode,0)
        spec=importlib.util.spec_from_file_location('audit_source_backend',ROOT/'scripts/group-build.py');backend=importlib.util.module_from_spec(spec);spec.loader.exec_module(backend)
        with patch.object(backend,'ROOT',root),patch.dict(os.environ,values):
            command=backend.configure_command('x86_64-linux-gnu-release','core',root/'fresh')
        self.assertIn('-DCPKT_DEPENDENCY_BUILD_JOBS=3',command);self.assertIn('-DCPKT_DEPENDENCY_CACHE='+custom,command)
        self.assertEqual(command[command.index('-G')+1],'Unix Makefiles')

    def test_deterministic_gzip_level_and_payload(self):
        from cpkt_packages import tar_stage
        import gzip
        prefix=self.work/'payload';prefix.mkdir();(prefix/'header.h').write_bytes(b'header '*10000);(prefix/'header.h').chmod(0o644)
        (prefix/'alias').symlink_to('header.h')
        archives=[self.work/'one.tar.gz',self.work/'two.tar.gz']
        original=gzip.GzipFile;levels=[]
        def compress(*args,**kwargs):levels.append(kwargs.get('compresslevel'));return original(*args,**kwargs)
        with patch('cpkt_packages.gzip.GzipFile',side_effect=compress):
            for path in archives:tar_stage(prefix,path)
        self.assertEqual(levels,[6,6]);self.assertEqual(archives[0].read_bytes(),archives[1].read_bytes())
        with tarfile.open(archives[0]) as archive:
            self.assertEqual(archive.extractfile('payload/header.h').read(),(prefix/'header.h').read_bytes())
            self.assertEqual(archive.getmember('payload/alias').linkname,'header.h')

    def test_editor_database_actual_commands_scope_coverage_and_atomic_failure(self):
        import cpkt_clangd_check as editor
        root=self.work/'editor';(root/'cmake').mkdir(parents=True)
        hover={};expected=[]
        for group in ('core',):
            graph=root/'build/x86_64-linux-gnu'/group/'Debug';graph.mkdir(parents=True)
            compiler=Path(sys.executable).resolve()
            source=root/(group+'.c');source.write_text('int '+group+';\n');hover[group]=[source.name]
            (graph/'CMakeCache.txt').write_text('CPKT_TARGET_ID:STRING=x86_64-linux-gnu\nCPKT_GROUP:STRING='+group+'\nCMAKE_BUILD_TYPE:STRING=Debug\nCMAKE_C_COMPILER:FILEPATH='+str(compiler)+'\nCMAKE_CXX_COMPILER:FILEPATH='+str(compiler)+'\n')
            header=graph/'generated/cpkt/owned.h';header.parent.mkdir(parents=True);header.write_text('int header;\n')
            entry={'directory':str(graph),'file':str(source),'arguments':[str(compiler),'-std=c89','-I'+str(header.parent),'-D'+group.upper(),'-c',str(source)]}
            expected.append(entry);(graph/'compile_commands.json').write_text(json.dumps([entry]))
        (root/'cmake/components.json').write_text(json.dumps({'hover':hover,'groups':dict.fromkeys(hover,{})}))
        with patch.object(editor,'delegated') as scope:editor.publish_editor_database(root,'x86_64-linux-gnu')
        scope.assert_called_once_with(root,'core')
        destination=root/'build/clangd/compile_commands.json';self.assertEqual(json.loads(destination.read_text()),expected)
        baseline=destination.read_bytes();(root/'build/x86_64-linux-gnu/core/Debug/compile_commands.json').write_text('[]')
        with patch.object(editor,'delegated'):self.fails(lambda:editor.publish_editor_database(root,'x86_64-linux-gnu'))
        self.assertEqual(destination.read_bytes(),baseline)
        with patch.object(editor,'delegated'):self.fails(lambda:editor.publish_editor_database(root,'armhf-linux-gnu'))


    def test_executable_consumer_orchestration_once_and_fresh_combinations(self):
        import cpkt_packages as packages
        owner=json.loads((ROOT/'cmake/components.json').read_text())['repository_group']
        prefix,manifests=self.sdk();root=self.work/'orchestration';root.mkdir()
        for directory in ('scripts','cmake','tests'):(root/directory).mkdir()
        shutil.copy2(ROOT/'CMakePresets.json',root/'CMakePresets.json')
        for name in ('validate-sdk.py','run-no-warnings.sh','cpkt-toolchains.sh'):
            shutil.copy2(ROOT/'scripts'/name,root/'scripts'/name)
        for name in ('auth_package_discovery_test.py','darwin_curl_package_test.sh'):
            if (ROOT/'tests'/name).is_file():shutil.copy2(ROOT/'tests'/name,root/'tests'/name)
        data={'repository_group':owner,'groups':{owner:{}},'components':{},
              'installed_examples':{},'installed_consumers':{}}
        (root/'tests'/f'{owner}.c').write_text('fixture '+owner)
        data['installed_consumers']['owned-'+owner]={'group':owner,'kind':'static',
            'source':'tests/'+owner+'.c','runtime_args':[],'pc':None}
        fixture=root/'scripts/cpkt_sdk_consumer.py'
        fixture.write_text("""import argparse,json,pathlib,subprocess,sys,hashlib,importlib.util
p=argparse.ArgumentParser()
for name in ('prefix','target','groups','owners','preset'):p.add_argument('--'+name,required=True)
p.add_argument('--composition',action='store_true');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('validator',"""+repr(str(ROOT/'scripts/validate-sdk.py'))+""");v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
v.validate(pathlib.Path(a.prefix),a.groups.split(','),expected_target=a.target)
with (root/'counts').open('a') as f:f.write(('probe:'+a.groups if a.composition else 'owned:'+a.owners)+'\\n')
namespace='all/composition-'+a.groups.replace(',','-') if a.composition else a.owners
outputs=root/'build/verification'/a.target/namespace/'installed-consumers';outputs.mkdir(parents=True,exist_ok=True)
name='probe-'+a.groups.replace(',','-') if a.composition else 'owned-'+a.owners
binary=outputs/name;binary.write_text('import sys;sys.exit(0)\\n')
cases=[]
for phase in range(2):
 subprocess.run([sys.executable,str(binary)],check=True)
 cases.append(dict(executable=name,executable_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),intentional_failure=False,arguments=[],target=a.target,status='passed',runtime='native'))
print(json.dumps(cases))
""")
        base=root/'archives';base.mkdir()
        target='x86_64-linux-gnu';ver='1.2.3'
        prerequisite=root/'core-archive.tar.gz'
        def pack(group,manifest=None,mutate=False):
            staging=root/('stage-'+group);staging.mkdir(exist_ok=True)
            owned=staging/packages.prefix_name(ver,target)
            if owned.exists():shutil.rmtree(owned)
            owned.mkdir()
            for entry in manifests[group]['files']:
                source=prefix/entry['path'];destination=owned/entry['path'];destination.parent.mkdir(parents=True,exist_ok=True)
                if source.is_symlink():destination.symlink_to(os.readlink(source))
                else:shutil.copy2(source,destination)
            destination=owned/'share/cpkt/packages'/f'{group}.json';destination.parent.mkdir(parents=True,exist_ok=True)
            destination.write_bytes(encoded(manifest or manifests[group]));destination.chmod(0o644)
            if mutate:(owned/(group+'/payload')).write_bytes(b'mutated independently')
            packages.tar_stage(owned,base/packages.archive_name(ver,target,owner) if group==owner else prerequisite)
        pack(owner)
        if owner!='core':pack('core')
        def invoke(args,**kwargs):
            result=subprocess.run(list(map(str,args)),capture_output=True,text=True)
            if result.returncode:raise ValueError(result.stderr)
            return result.stdout
        configured={'CMAKE_C_COMPILER':sys.executable,'CMAKE_CXX_COMPILER':sys.executable,
                    'CPKT_DEPENDENCY_BUILD_JOBS':'1'}
        orders=[['core']] if owner=='core' else [['core',owner],[owner,'core']]
        counters=lambda:(root/'counts').read_text().splitlines()
        with patch.object(packages,'ROOT',root),patch.object(packages,'load',return_value=data),patch.object(packages,'command',side_effect=invoke),patch.object(packages,'prepared_core',return_value=(prerequisite,manifests['core'])),patch.object(packages,'extract_core',side_effect=lambda archive,parent,version,target:packages.safe_extract(archive,parent,packages.prefix_name(version,target))),patch('cpkt_sdk_consumer.configuration',return_value=configured),patch.dict(os.environ,CPKT_OPERATION_RUN='fixture-current'):
            results=packages.combinations(target+'-release',ver,base)
            self.assertEqual([item['order'] for item in results],orders)
            self.assertEqual(counters()[0],'owned:'+owner)
            self.assertEqual(len(counters()),1+len(orders))
            packages.combinations(target+'-release',ver,base)
            self.assertEqual(counters().count('owned:'+owner),1)
            self.assertEqual(len(counters()),1+2*len(orders))
            proof=json.loads((root/'build/verification'/target/'all/packages/proof.json').read_text())
            self.assertEqual(proof['owned_suite_accounting'],{owner:'reused'})
            self.assertEqual(set(proof['owned_suites']),{owner})
            packages.combinations(target+'-release',ver,base,reuse_owned=False)
            self.assertEqual(counters().count('owned:'+owner),2)
            (root/'build/verification'/target/owner/'installed-consumers'/('owned-'+owner)).unlink()
            packages.combinations(target+'-release',ver,base)
            self.assertEqual(counters().count('owned:'+owner),3)
            with patch.dict(os.environ,CPKT_OPERATION_RUN='new-independent-run'):
                packages.combinations(target+'-release',ver,base)
            self.assertEqual(counters().count('owned:'+owner),4)
            # Every run rechecks archive bytes, even with reusable owner proofs.
            pack(owner,mutate=True)
            with self.assertRaisesRegex(ValueError,'content/mode mismatch'):
                packages.combinations(target+'-release',ver,base)
            pack(owner)
            if owner!='core':
                self.assertNotIn('owned:core',counters())
                wrong=copy.deepcopy(manifests[owner]);wrong['requires_core']['package_id']='c'*64
                wrong.pop('package_id');wrong['package_id']=digest(encoded(wrong));pack(owner,manifest=wrong)
                with self.assertRaisesRegex(ValueError,'exact core package requirement'):
                    packages.combinations(target+'-release',ver,base)
        print('orchestration: one owned suite per run, fresh composition probes, exact outputs/bytes/prerequisite checks')


    def test_summary_default_ids_aliases_and_universal_cache_publication(self):
        prefix,manifests=self.sdk();library=prefix/'lib';library.mkdir()
        for name in ('libnative.a','libnative.so.4','libnative.4.dylib'):
            (library/name).write_bytes(name.encode());(library/name).chmod(0o644)
        (library/'libnative.so').symlink_to('libnative.so.4');(library/'libnative.dylib').symlink_to('libnative.4.dylib')
        manifest=copy.deepcopy(manifests['core'])
        for path in sorted(library.iterdir()):
            manifest['files'].append({'path':'lib/'+path.name,'type':'symlink','target':os.readlink(path)} if path.is_symlink() else {'path':'lib/'+path.name,'type':'file','mode':'0644','sha256':digest(path.read_bytes())})
        manifest['files'].sort(key=lambda e:e['path']);core=self.write_manifest(prefix,'core',manifest,True)
        catalog=prefix/'share/cpkt/payload-ownership.json'
        # The pre-existing optional payloads remain unselected via their catalog.
        args=[sys.executable,str(ROOT/'scripts/validate-sdk.py'),'--prefix',str(prefix),'--groups','core']
        ordinary=json.loads(subprocess.check_output(args,text=True));self.assertEqual(ordinary,{'core':core['package_id']})
        summary=json.loads(subprocess.check_output(args+['--cmake-summary'],text=True))
        self.assertEqual(summary['package_ids'],ordinary);self.assertEqual(summary['library_names'].split(';'),sorted(p.name for p in library.iterdir()))
        payload=b'universal transport cache';asset={'id':7,'size':len(payload),'sha256':digest(payload)}
        api=unittest.mock.Mock();api.open.return_value=io.BytesIO(payload)
        shared=self.work/'universal';path=acquire(api,asset,shared);self.assertEqual(api.open.call_count,1)
        script=self.work/'acquire.cmake'
        script.write_text('cmake_minimum_required(VERSION 3.21)\nset(CPKT_DEPENDENCY_CACHE "'+str(shared)+'")\nset(CPKT_DEPENDENCY_CACHE_LOCK_TIMEOUT 1)\ninclude("'+str(ROOT/'cmake/CpktDependencyArchiveCache.cmake')+'")\ncpkt_acquire_dependency_archive(found NAME another-name.tar.gz SHA256 '+asset['sha256']+' URLS https://unreachable.invalid/archive)\nfile(SHA256 "${found}" digest)\nif(NOT digest STREQUAL "'+asset['sha256']+'")\nmessage(FATAL_ERROR "universal cache bytes differ")\nendif()\n')
        result=subprocess.run(['cmake','-P',str(script)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr);self.assertEqual(api.open.call_count,1)

    def test_original_header_native_leak_assertions_preserved(self):
        import re
        data=json.loads((ROOT/'cmake/components.json').read_text())
        expected={'lua_runtime.h':['lua_State','lua_Integer','lua_Number','lua_Unsigned','long long','inline'],
                  'audio.h':['miniaudio','ma_','stdint\\.h','stdbool\\.h','uint8_t','uint16_t','uint32_t','uint64_t','int8_t','int16_t','int32_t','int64_t','long long','inline'],
                  'sus.h':['whisper','ggml','stdint\\.h','stdbool\\.h','uint8_t','uint16_t','uint32_t','uint64_t','int8_t','int16_t','int32_t','int64_t','long long','inline'],
                  'postgres.h':['libpq','postgres_ext','PGconn','PGresult','PGcancel','stdint\\.h','stdbool\\.h','uint64_t','int64_t','long long','inline'],
                  'gssapi.h':['gssapi/','stdint\\.h','stdbool\\.h','uint32_t','int32_t','long long','inline']}
        actual={header:patterns for component in data['components'].values() for facade in component['package']['facades'] for header,patterns in facade.get('header_forbidden',{}).items()}
        for header,tokens in expected.items():
            if header not in actual:continue
            self.assertTrue(set(tokens)<=set(actual[header]),header)
            for token in tokens:
                candidate=token.replace('\\.','.')
                self.assertTrue(any(re.search(pattern,candidate) for pattern in actual[header]),(header,token))
        # Audio/speech intentionally support guarded C++ linkage framing.
        for header in ('audio.h','sus.h','lua_runtime.h'):
            if header not in actual:continue
            self.assertFalse(any(re.search(pattern,'#ifdef __cplusplus\nextern "C" {\n#endif\n') for pattern in actual[header]))


    def test_composition_joint_packages_follow_actual_inventory(self):
        from cpkt_sdk_consumer import composition_records
        data=json.loads((ROOT/'cmake/components.json').read_text())
        owner=data['repository_group']
        groups=['core'] if owner=='core' else ['core',owner]
        records=composition_records(data,groups)
        self.assertTrue(records)
        self.assertTrue(any(item['kind']=='pic' for item in records.values()))
        for name,item in records.items():
            self.assertEqual(item,data['installed_consumers'][name])
            self.assertEqual(item['group'],owner)
            self.assertTrue((ROOT/item['source']).is_file())
        with self.assertRaisesRegex(ValueError,'omits the owning SDK'):
            composition_records(data,[])

    def test_independent_component_notices_cannot_be_removed_by_resigning(self):
        from cpkt_sdk_consumer import inspect_notices
        prefix,manifests=self.sdk()
        for group in ('core','db','misc'):
            docs=prefix/'share/doc/cpkt'/group
            license_path=docs/'third_party'/(group+'-native')/'LICENSE';license_path.parent.mkdir(parents=True);license_path.write_text('upstream license')
            (docs/'LICENSE').write_text('bundle license');(docs/'README.md').write_text('sdk notice')
            (docs/'THIRD_PARTY_NOTICES.md').write_text(group+'-native 9.2')
        inspect_notices(prefix,['core','db','misc'])
        (prefix/'share/doc/cpkt/db/third_party/db-native/LICENSE').unlink()
        with self.assertRaisesRegex(ValueError,'component notice/license'):inspect_notices(prefix,['db'])


if __name__=='__main__':unittest.main()
