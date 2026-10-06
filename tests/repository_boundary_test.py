#!/usr/bin/env python3
"""Falsifiable repository ownership, provider identity and offline SDK contracts."""
import copy
import hashlib
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

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from cpkt_inventory import load,components_for,REPOSITORY_GROUP
from cpkt_packages import validator,artifacts
from generated_output_ancestry_test import GeneratedOutputMutation


def canonical(item):return json.dumps(item,sort_keys=True,separators=(',',':')).encode()
def sha(item):return hashlib.sha256(item).hexdigest()

class Repository(unittest.TestCase):
    def test_core_cannot_acquire_itself(self):
        from native_lifecycle_fixture import seed,environment
        from cpkt_receipts import tree_identity
        with tempfile.TemporaryDirectory(prefix='self-acquisition-',dir=ROOT/'build') as temporary:
            root=Path(temporary);seed(root)
            before=tree_identity(root)
            result=subprocess.run(['bash',str(root/'scripts/core-dependency.sh'),'x86_64-linux-gnu'],
                env=environment(),capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('cpkt produces core',result.stderr)
            self.assertIn('make deps-all',result.stderr)
            self.assertEqual(before,tree_identity(root))
            self.assertFalse((root/'build').exists())
            self.assertFalse((root/'.cache').exists())

    def test_dependency_graph_has_no_owned_sibling(self):
        data=load(ROOT)
        self.assertEqual({REPOSITORY_GROUP},set(data['groups']))
        names=components_for(data,'all')
        self.assertTrue(names)
        self.assertEqual(set(names),{name for name,item in data['components'].items() if item['group']==REPOSITORY_GROUP})
        for name in names:
            for edge in data['components'][name]['dependencies']:
                self.assertIn(data['components'][edge]['group'],(REPOSITORY_GROUP,'core'))
                if data['components'][edge]['group']!=REPOSITORY_GROUP:self.assertEqual('cpkt',data['components'][edge]['external'])

    def test_missing_mock_header_fails_before_build(self):
        from cpkt_inventory import validate_inputs
        data=load(ROOT)
        (ROOT/'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='mock-input-preflight-',dir=ROOT/'build') as temporary:
            root=Path(temporary)
            for name in validate_inputs(ROOT,data):
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(ROOT/name,path)
            (root/'tests/lua_mock/include/lauxlib.h').unlink()
            with self.assertRaisesRegex(RuntimeError,'lua_mock/include/lauxlib.h'):
                validate_inputs(root,data)
            self.assertFalse((root/'.cache').exists())
            self.assertFalse((root/'build').exists())

    def test_missing_fixture_input_fails_before_build(self):
        from cpkt_inventory import validate_inputs
        data=load(ROOT)
        with tempfile.TemporaryDirectory(prefix='fixture-input-preflight-',dir=ROOT/'build') as temporary:
            root=Path(temporary)
            for name in validate_inputs(ROOT,data):
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(ROOT/name,path)
            shutil.rmtree(root/'tests/curl_system_truststore_fixture')
            with self.assertRaisesRegex(RuntimeError,'curl_system_truststore_fixture'):
                validate_inputs(root,data)
            self.assertFalse((root/'.cache').exists())
            self.assertFalse((root/'build').exists())

    def test_hardening_inputs_fail_before_build_when_missing_or_empty(self):
        from cpkt_inventory import validate_inputs
        data=load(ROOT)
        policy=data['groups'][REPOSITORY_GROUP]['hardening']
        paths=[policy['memcheck_suppression'],*policy['fuzz_seeds']]
        self.assertEqual(REPOSITORY_GROUP!='db',bool(policy['fuzz_seeds']))
        (ROOT/'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='hardening-input-preflight-',dir=ROOT/'build') as temporary:
            root=Path(temporary)
            for name in validate_inputs(ROOT,data):
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(ROOT/name,path)
            for name in paths:
                with self.subTest(missing=name):
                    path=root/name;original=path.read_bytes();path.unlink()
                    with self.assertRaisesRegex(RuntimeError,name):validate_inputs(root,data)
                    self.assertFalse((root/'.cache').exists())
                    self.assertFalse((root/'build').exists())
                    path.write_bytes(original)
            for name in policy['fuzz_seeds']:
                with self.subTest(empty=name):
                    path=root/name;original=path.read_bytes();path.write_bytes(b'')
                    with self.assertRaisesRegex(RuntimeError,'fuzz seed must be nonempty'):
                        validate_inputs(root,data)
                    path.write_bytes(original)

    def test_pic_requirement_distinguishes_packaging_from_hardening(self):
        data=load(ROOT)
        (ROOT/'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='pic-coverage-policy-',dir=ROOT/'build') as temporary:
            root=Path(temporary);(root/'cmake').mkdir()
            (root/'cmake/components.json').write_text(json.dumps({
                'tests':{'static_archive_pic_link':data['tests']['static_archive_pic_link']}}))
            for name,facade_only,registered,success in (
                    ('hardening','ON',False,True),
                    ('ordinary-missing','OFF',False,False),
                    ('ordinary-covered','OFF',True,True)):
                source=root/name;source.mkdir()
                source.joinpath('CMakeLists.txt').write_text(
                    'cmake_minimum_required(VERSION 3.21)\nproject(coverage NONE)\nenable_testing()\n'
                    'set(CPKT_BUILD_TESTS ON)\nset(CPKT_CAN_RUN_TARGET_EXECUTABLES ON)\n'
                    'set(CPKT_FACADE_ONLY '+facade_only+')\n'+
                    ('add_test(NAME static_archive_pic_link COMMAND cmake -E true)\n' if registered else '')+
                    'set(CPKT_GROUP '+REPOSITORY_GROUP+')\nfile(READ "'+str(root/'cmake/components.json')+'" CPKT_INVENTORY)\ninclude("'+str(ROOT/'cmake/CpktTestInventory.cmake')+'")\ncpkt_assert_test_inventory()\n')
                result=subprocess.run(['cmake','-S',str(source),'-B',str(root/(name+'-build'))],capture_output=True,text=True)
                self.assertEqual(success,result.returncode==0,result.stdout+result.stderr)
                if not success:self.assertIn('Missing required inventory case: static_archive_pic_link',result.stderr)

    def test_release_asset_inventory_is_one_producer(self):
        data=load(ROOT);project=data['project']
        expected={project+'-1.2.3-'+target+'.tar.gz' for target in data['package_targets']}
        expected|={project+'-1.2.3.tar.gz',project+'-1.2.3-arm64-apple-darwin-smoke-test.zip'}
        self.assertEqual(expected,set(artifacts('1.2.3','release')))
        self.assertEqual(9,len(expected))

    def test_independent_version_requires_exact_core_identity(self):
        (ROOT/'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='provider-identity-',dir=ROOT/'build') as temporary:
            prefix=Path(temporary);manifests={}
            for group,version in (('core','0.1.0'),('db','0.7.2')):
                path=prefix/group/'payload';path.parent.mkdir();path.write_bytes(group.encode());path.chmod(0o644)
                item={'schema_version':1,'group':group,'release_version':version,'target_id':'x86_64-linux-gnu','libc':'gnu','macos_deployment_target':None,
                    'components':[{'name':group,'version':'1','source_sha256':'a'*64,'features':{},'abi':{'soname':'lib'+group+'.so.0'}}],
                    'files':[{'path':group+'/payload','type':'file','mode':'0644','sha256':sha(path.read_bytes())}],
                    'requires_core':None if group=='core' else {k:manifests['core'][k] for k in ('package_id','release_version','target_id')}}
                item['package_id']=sha(canonical(item));manifests[group]=item
                destination=prefix/'share/cpkt/packages'/(''+group+'.json');destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(canonical(item));destination.chmod(0o644)
            self.assertEqual({'core','db'},set(validator.validate(prefix,['core','db'],'0.7.2','x86_64-linux-gnu')))
            item=copy.deepcopy(manifests['db']);item['requires_core']['package_id']='f'*64;item.pop('package_id');item['package_id']=sha(canonical(item))
            (prefix/'share/cpkt/packages/db.json').write_bytes(canonical(item))
            with self.assertRaisesRegex(ValueError,'exact core'):validator.validate(prefix,['core','db'])
            (prefix/'share/cpkt/packages/db.json').write_bytes(canonical(manifests['db']))
            (prefix/'core/payload').write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError,'content/mode'):validator.validate(prefix,['core','db'])

    @unittest.skipIf(REPOSITORY_GROUP=='core','core has no external provider')
    def test_missing_pin_fails_before_acquisition_or_mutation(self):
        from native_lifecycle_fixture import seed,environment
        from cpkt_receipts import tree_identity
        (ROOT/'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='missing-pin-',dir=ROOT/'build') as temporary:
            root=Path(temporary);seed(root);(root/'dependencies').mkdir()
            (root/'dependencies/cpkt.json').write_text(json.dumps({'schema_version':1,'repository':'c89systems/cpkt','version':'0.1.0','targets':{}}))
            marker=root/'acquisition-started';cmake=root/'cmake-probe.sh'
            cmake.write_text('#!/bin/sh\ncase "$*" in *CPKT_CORE_ACTION=acquire*) touch '+str(marker)+'; exit 47 ;; esac\nexec '+shutil.which('cmake')+' "$@"\n');cmake.chmod(0o755)
            before=tree_identity(root)
            result=subprocess.run(['bash',str(root/'scripts/core-dependency.sh'),'x86_64-linux-gnu'],env=dict(environment(),CMAKE=str(cmake)),capture_output=True,text=True)
            self.assertNotEqual(0,result.returncode);self.assertIn('no published checksum pin',result.stderr)
            self.assertFalse(marker.exists());self.assertFalse((root/'build').exists());self.assertFalse((root/'.cache').exists())
            self.assertEqual(before,tree_identity(root))



class GeneratedWorkspace(unittest.TestCase):
    """Exercise public Bash writers in miniature repositories, without SDKs."""
    def fixture(self, root):
        from native_lifecycle_fixture import seed, environment
        seed(root)
        tools = root / 'tools'
        tools.mkdir()
        calls = root / 'children.jsonl'
        cmake = tools / 'cmake'
        cmake.write_text('#!' + sys.executable + '\n' +
            'import json,subprocess,sys\nfrom pathlib import Path\n'
            'a=sys.argv[1:]\n'
            'if "--list-presets=configure" in a or ("-P" in a and not any("producer-cache" in v for v in a)):\n'
            '  raise SystemExit(subprocess.call([' + repr(shutil.which('cmake')) + ',*a]))\n'
            'with open(' + repr(str(calls)) + ',"a") as f:f.write(json.dumps(a)+"\\n")\n'
            'if "-B" in a:Path(a[a.index("-B")+1]).mkdir(parents=True,exist_ok=True)\n'
            'if "--install" in a:\n'
            '  p=Path(a[a.index("--prefix")+1]);p.mkdir(parents=True,exist_ok=True);(p/"payload").write_text("fixture")\n')
        cmake.chmod(0o755)
        ctest = tools / 'ctest'
        ctest.write_text('#!' + sys.executable + '\nimport json,sys\n'
            'with open(' + repr(str(calls)) + ',"a") as f:f.write(json.dumps(sys.argv[1:])+"\\n")\n'
            'print("{\\"tests\\": []}")\n')
        ctest.chmod(0o755)
        (tools / 'valgrind').write_text('#!/bin/sh\nexit 91\n')
        (tools / 'valgrind').chmod(0o755)
        for name in ('cpkt_build_evidence.py', 'cpkt_memcheck_evidence.py', 'cpkt_package_manifest.py'):
            (root / 'scripts' / name).write_text('# inert evidence/manifest fixture\n')
        (root / 'scripts/test-e2e.sh').write_text('#!/bin/sh\nexit 0\n')
        (root / 'scripts/cpkt-toolchains.sh').write_text('#!/bin/sh\nprintf "status=ready\\n"\n')
        env = dict(environment(), CMAKE=str(cmake), CTEST=str(ctest),
                   PATH=str(tools) + os.pathsep + os.environ['PATH'],
                   CPKT_DEPENDENCY_CACHE=str(root / 'shared-cache'))
        return env, calls

    def cases(self):
        graph = 'build/x86_64-linux-gnu/' + REPOSITORY_GROUP
        provider = {'core': 'cpkt', 'db': 'cpktdb', 'misc': 'cpktmisc'}[REPOSITORY_GROUP]
        archive = provider + '-1.2.3-x86_64-linux-gnu.tar.gz'
        stage = 'build/package-stage/x86_64-linux-gnu/' + REPOSITORY_GROUP
        return [
            ('preflight.sh', ['--preset', 'debug'], 'build/control/preflight/x86_64-linux-gnu/native', True),
            ('build.sh', ['test', '--preset', 'debug'], graph + '/Debug/cpkt-test-inventory.json', False),
            ('memcheck.sh', [], graph + '/Valgrind/cpkt-memcheck-inventory.json', False),
            ('package-stage.sh', ['--preset', 'release'], stage + '/archives/' + archive, False),
            ('package-stage.sh', ['--preset', 'release'], stage + '/archives/' + archive + '.tmp', False),
            ('source-reconstruct.sh', [], 'build/source-composition/' + archive, False),
            ('package.sh', ['package'], 'dist/' + archive, False),
            ('darwin.sh', ['test-darwin-native'], 'dist/' + provider + '-1.2.3-arm64-apple-darwin.tar.gz', False),
            ('package-source.sh', [], 'dist/' + provider + '-1.2.3.tar.gz', False),
            ('source-archive-verify.sh', [provider + '-1.2.3.tar.gz', '1.2.3'],
             'build/verification/source/1.2.3/reconstruction.log', False),
            ('run-no-warnings.sh', ['probe', '/bin/true'], 'build', True),
        ]

    def test_writers_refuse_redirects_before_children_and_preserve_sentinels(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        for script, arguments, relative, directory in self.cases():
            for redirect in ('leaf', 'parent', 'dangling'):
                if relative == 'build' and redirect == 'parent':
                    continue
                with self.subTest(script=script, path=relative, redirect=redirect), \
                        tempfile.TemporaryDirectory(prefix='workspace-boundary-', dir=ROOT / 'build') as temporary:
                    work = Path(temporary)
                    root = work / 'repo'
                    env, calls = self.fixture(root)
                    external = work / 'unrelated'
                    external.mkdir()
                    sentinel = external / 'sentinel'
                    sentinel.write_bytes(b'unrelated bytes\n')
                    sentinel.chmod(0o640)
                    target = root / relative
                    if redirect == 'parent':
                        target = target.parent
                    target.parent.mkdir(parents=True, exist_ok=True)
                    destination = external if directory or redirect == 'parent' else sentinel
                    if redirect == 'dangling':
                        destination = work / 'absent'
                    target.symlink_to(destination, target_is_directory=directory or redirect == 'parent')
                    provider = {'core': 'cpkt', 'db': 'cpktdb', 'misc': 'cpktmisc'}[REPOSITORY_GROUP]
                    (root / (provider + '-1.2.3.tar.gz')).write_bytes(b'not extracted')
                    # Reconstruction/export child scripts would start production work.
                    # A marker proves refusal occurs before any such invocation.
                    if script == 'darwin.sh':
                        uname = root / 'tools/uname'
                        uname.write_text('#!/bin/sh\ncase "$1" in -s) echo Darwin ;; -m) echo arm64 ;; esac\n')
                        uname.chmod(0o755)
                    if script in ('source-reconstruct.sh', 'package.sh', 'darwin.sh'):
                        (root / 'scripts/build.sh').write_text('#!/bin/sh\ntouch "' + str(calls) + '"\nexit 47\n')
                        (root / 'scripts/core-dependency.sh').write_text('#!/bin/sh\ntouch "' + str(calls) + '"\nexit 47\n')
                    result = subprocess.run(['bash', str(root / 'scripts' / script), *arguments],
                                            cwd=root, env=env, capture_output=True, text=True)
                    self.assertEqual(2, result.returncode, result.stdout + result.stderr)
                    self.assertIn('symlink ancestor', result.stderr)
                    self.assertFalse(calls.exists(), result.stdout + result.stderr)
                    self.assertEqual(b'unrelated bytes\n', sentinel.read_bytes())
                    self.assertEqual(0o640, sentinel.stat().st_mode & 0o777)
                    self.assertEqual([sentinel], list(external.iterdir()))
                    self.assertFalse((work / 'absent').exists())
                    self.assertTrue(target.is_symlink())
                    self.assertFalse((root / '.cache').exists())

    def test_ordinary_preflight_inventory_and_stage_keep_native_arguments(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        for script, arguments in [('preflight.sh', ['--preset', 'debug']),
                                  ('build.sh', ['test', '--preset', 'debug', '--regex', 'some test', '--label', 'fixture']),
                                  ('memcheck.sh', ['--regex', 'some test']),
                                  ('package-stage.sh', ['--preset', 'release'])]:
            with self.subTest(script=script), tempfile.TemporaryDirectory(
                    prefix='workspace-ordinary-', dir=ROOT / 'build') as temporary:
                root = Path(temporary)
                env, calls = self.fixture(root)
                (root / 'build/x86_64-linux-gnu' / REPOSITORY_GROUP / 'Valgrind').mkdir(parents=True)
                result = subprocess.run(['bash', str(root / 'scripts' / script), *arguments],
                                        cwd=root, env=env, capture_output=True, text=True)
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                argv = [json.loads(line) for line in calls.read_text().splitlines()]
                if script == 'build.sh':
                    self.assertIn(['-R', 'some test'], [a[i:i+2] for a in argv for i in range(len(a)-1)])
                    self.assertIn(['-L', 'fixture'], [a[i:i+2] for a in argv for i in range(len(a)-1)])
                if script == 'memcheck.sh':
                    self.assertTrue(any('-T' in a and 'memcheck' in a for a in argv))
                if script == 'preflight.sh':
                    self.assertTrue(any('-B' in a and str(root / 'build/control/preflight/x86_64-linux-gnu/native') in a for a in argv))
                if script == 'package-stage.sh':
                    archives = list((root / 'build/package-stage/x86_64-linux-gnu' / REPOSITORY_GROUP / 'archives').glob('*.tar.gz'))
                    self.assertEqual(1, len(archives))
                    listing = subprocess.check_output(['tar', 'tf', str(archives[0])], text=True)
                    self.assertIn('/payload', listing)

    def test_generic_archive_accepts_explicit_output_and_warning_workspace_is_private(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='workspace-contract-', dir=ROOT / 'build') as temporary:
            root = Path(temporary) / 'repo'
            env, calls = self.fixture(root)
            prefix = root / 'payload'
            prefix.mkdir()
            (prefix / 'bytes').write_text('fixture')
            output = Path(temporary) / 'explicit-output/archive.tar.gz'
            result = subprocess.run(['bash', str(root / 'scripts/archive.sh'), str(prefix), str(output)],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertTrue(output.is_file())
            result = subprocess.run(['bash', str(root / 'scripts/run-no-warnings.sh'), 'probe', '/bin/true'],
                                    cwd=prefix, env=env, capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertFalse(list((root / 'build').glob('cpkt-no-warnings.*')))


    def test_source_composition_copies_owned_archive_before_compose(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='workspace-composition-', dir=ROOT / 'build') as temporary:
            root = Path(temporary)
            env, calls = self.fixture(root)
            provider = {'core': 'cpkt', 'db': 'cpktdb', 'misc': 'cpktmisc'}[REPOSITORY_GROUP]
            archive_name = provider + '-1.2.3-x86_64-linux-gnu.tar.gz'
            artifact = root / 'build/package-stage/x86_64-linux-gnu' / REPOSITORY_GROUP / 'archives' / archive_name
            artifact.parent.mkdir(parents=True)
            artifact.write_bytes(b'owned archive bytes')
            for name in ('build.sh', 'core-dependency.sh', 'package.sh'):
                (root / 'scripts' / name).write_text('#!/bin/sh\nexit 0\n')
            (root / 'scripts/cpkt_packages.py').write_text(
                'import sys\nfrom pathlib import Path\n'
                'assert sys.argv[1:]==["compose","--group","all","--preset","x86_64-linux-gnu-release","--base",' + repr(str(root / 'build/source-composition')) + ']\n'
                'assert (Path(sys.argv[-1])/' + repr(archive_name) + ').read_bytes()==b"owned archive bytes"\n')
            result = subprocess.run(['bash', str(root / 'scripts/source-reconstruct.sh')],
                                    cwd=root / 'tools', env=env, capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertEqual(b'owned archive bytes', (root / 'build/source-composition' / archive_name).read_bytes())

    def test_source_archive_output_stays_in_dist(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='workspace-source-', dir=ROOT / 'build') as temporary:
            root = Path(temporary)
            env, calls = self.fixture(root)
            (root / 'scripts/cpkt_source_proof.py').write_text('# inert source-proof fixture\n')
            (root / 'tests').mkdir()
            (root / 'tests/privacy_scan.cmake').write_text('# inert privacy scan fixture\n')
            (root / 'RELEASE_MANIFEST').write_text('scripts/package-source.sh\nscripts/lifecycle-common.sh\ncmake/components.json\n')
            result = subprocess.run(['bash', str(root / 'scripts/package-source.sh')],
                                    cwd=root / 'tools', env=env, capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            provider = {'core': 'cpkt', 'db': 'cpktdb', 'misc': 'cpktmisc'}[REPOSITORY_GROUP]
            archives = list((root / 'dist').glob('*.tar.gz'))
            self.assertEqual([root / 'dist' / (provider + '-1.2.3.tar.gz')], archives)
            listing = subprocess.check_output(['tar', 'tf', str(archives[0])], text=True)
            self.assertIn('/VERSION', listing)
            self.assertIn('/RELEASE_MANIFEST', listing)
            self.assertFalse(list((root / 'build').glob('cpkt-source-stage.*')))

class NativeMetadataMutation(unittest.TestCase):
    """Authenticated native mutations must refuse before erasing unrelated state."""
    target = 'x86_64-linux-gnu'

    def fixture(self, work):
        from native_lifecycle_fixture import seed, environment
        root = work/'repo'; seed(root)
        external = work/'foreign'; external.mkdir()
        sentinel = external/'sentinel'; sentinel.write_bytes(b'foreign bytes\n'); sentinel.chmod(0o640)
        marker = root/'child-marker'
        child = root/'child.sh'
        child.write_text('#!/bin/sh\nprintf child > "'+str(marker)+'"\n')
        child.chmod(0o755)
        return root, external, sentinel, child, environment()

    def operation(self, root, env, cwd, args):
        return subprocess.run(['bash',str(root/'scripts/operation.sh'),'--group',REPOSITORY_GROUP,'--',
            'bash','-c','cd "$1"; shift; exec "$@"','fixture',str(cwd),*map(str,args)],
            cwd=root,env=env,capture_output=True,text=True)

    def identity(self, path):
        from cpkt_receipts import file_identity
        return file_identity(path)

    def refuse(self, result, root, sentinel, link, sibling=None):
        self.assertNotEqual(0,result.returncode,result.stdout+result.stderr)
        self.assertIn('symlink ancestor',result.stderr.lower())
        self.assertEqual(b'foreign bytes\n',sentinel.read_bytes())
        self.assertEqual(0o640,sentinel.stat().st_mode & 0o777)
        self.assertTrue(link.is_symlink())
        self.assertFalse((root/'child-marker').exists())
        if sibling:
            self.assertEqual(b'sibling evidence\n',sibling.read_bytes())
            self.assertEqual(0o600,sibling.stat().st_mode & 0o777)

    def test_authenticated_launcher_and_native_clean_refuse_redirects(self):
        for route in ('launcher','component','clean','producer-clean'):
            for kind in ('verification','target','group','leaf','dangling','parent-dangling'):
                with self.subTest(route=route,kind=kind), tempfile.TemporaryDirectory(dir=ROOT/'build') as tmp:
                    root,external,sentinel,child,env=self.fixture(Path(tmp))
                    graph=root/'build'/self.target/REPOSITORY_GROUP/'Debug'; graph.mkdir(parents=True)
                    evidence=root/'build/verification'/self.target/REPOSITORY_GROUP
                    leaf=evidence/('component-toy.json' if route=='component' else 'Debug-built.json')
                    if kind in ('leaf','dangling'):
                        evidence.mkdir(parents=True)
                        sibling=evidence/'Debug-development.json'; sibling.write_bytes(b'sibling evidence\n'); sibling.chmod(0o600)
                        leaf.symlink_to(sentinel if kind=='leaf' else external/'absent'); link=leaf
                    else:
                        link={'verification':root/'build/verification','target':evidence.parent,'group':evidence,'parent-dangling':evidence}[kind]
                        link.parent.mkdir(parents=True,exist_ok=True)
                        link.symlink_to(external/'absent' if kind=='parent-dangling' else external,target_is_directory=True)
                        sibling=external/'Debug-development.json'; sibling.write_bytes(b'sibling evidence\n'); sibling.chmod(0o600)
                    if route in ('clean','producer-clean'):
                        (graph/'CMakeCache.txt').write_text('CPKT_TARGET_ID:STRING='+self.target+'\nCPKT_GROUP:STRING='+REPOSITORY_GROUP+'\nCPKT_NATIVE_MAKE_PROGRAM:FILEPATH='+str(child)+'\nCPKT_DEPENDENCY_PRODUCER:BOOL='+('ON' if route=='producer-clean' else 'OFF')+'\n')
                        args=['bash',root/'scripts/native-build.sh','clean']
                    else:
                        if route=='component':
                            graph=root/'.cache/deps-build'/self.target/'toy'; graph.mkdir(parents=True)
                        args=['bash',root/'scripts/build-guard.sh',root,REPOSITORY_GROUP,child]
                    result=self.operation(root,env,graph,args)
                    self.refuse(result,root,sentinel,link,sibling)
                    self.assertFalse((external/'absent').exists())

    def test_native_selection_and_control_exemptions(self):
        for route in ('launcher','component','clean','producer-clean','check','package-stage'):
            with self.subTest(route=route),tempfile.TemporaryDirectory(dir=ROOT/'build') as tmp:
                root,external,sentinel,child,env=self.fixture(Path(tmp))
                graph=root/'build'/self.target/REPOSITORY_GROUP/'Debug'; graph.mkdir(parents=True)
                evidence=root/'build/verification'/self.target/REPOSITORY_GROUP; evidence.mkdir(parents=True)
                names=('Debug-development.json','Debug-built.json','Release-development.json','Release-built.json','component-toy.json','component-other.json')
                for name in names:(evidence/name).write_text(name)
                sibling=root/'build/verification'/self.target/'other'/'Debug-development.json'; sibling.parent.mkdir(); sibling.write_text('other group')
                before={n:self.identity(evidence/n) for n in names}
                if route in ('clean','producer-clean'):
                    (graph/'CMakeCache.txt').write_text('CPKT_TARGET_ID:STRING='+self.target+'\nCPKT_GROUP:STRING='+REPOSITORY_GROUP+'\nCPKT_NATIVE_MAKE_PROGRAM:FILEPATH='+str(child)+'\nCPKT_DEPENDENCY_PRODUCER:BOOL='+('ON' if route=='producer-clean' else 'OFF')+'\n')
                    args=['bash',root/'scripts/native-build.sh','clean']
                    removed=set(names if route=='producer-clean' else names[:4])
                else:
                    args=['bash',root/'scripts/build-guard.sh',root,REPOSITORY_GROUP]
                    removed=set(names[:2])
                    if route=='component':
                        graph=root/'.cache/deps-build'/self.target/'toy'; graph.mkdir(parents=True)
                        removed={'Debug-development.json','Release-development.json','component-toy.json'}
                    if route=='check':
                        args+=['bash',root/'scripts/operation.sh','--root',root,'--group',REPOSITORY_GROUP,'--check']; removed=set()
                    elif route=='package-stage':
                        (root/'scripts/package.sh').write_text(child.read_text())
                        args+=['bash',root/'scripts/package.sh','package-stage','--group',REPOSITORY_GROUP,'--preset','release','--scope','selected']; removed=set()
                    else:args+=[child]
                result=self.operation(root,env,graph,args)
                self.assertEqual(0,result.returncode,result.stdout+result.stderr)
                self.assertEqual(set(names)-removed,{p.name for p in evidence.iterdir()})
                for name in set(names)-removed:self.assertEqual(before[name],self.identity(evidence/name))
                self.assertEqual('other group',sibling.read_text())

    def toy_contract(self, root, child):
        script=root/'contract.cmake'
        script.write_text('cmake_minimum_required(VERSION 3.21)\n'
            'set(CMAKE_SOURCE_DIR "'+str(root)+'")\nset(CMAKE_BINARY_DIR "'+str(root/'build/graph')+'")\n'
            'set(CPKT_TARGET_ID '+self.target+')\nset(CPKT_GROUP '+REPOSITORY_GROUP+')\n'
            'set(CPKT_BUILD_DEPENDENCIES ON)\nset(CPKT_DEPENDENCY_PRODUCER ON)\n'
            'set(CPKT_EXTERNAL_ROOT_LIFECYCLE_OWNED ON)\nset(CPKT_DEPENDENCY_BUILD_ROOT_LIFECYCLE_OWNED ON)\n'
            'set(CPKT_DEPENDENCY_CONTRACT_ROOT "'+str(root/'.cache/dependency-contracts')+'")\n'
            'set(CPKT_HOST_PYTHON_EXECUTABLE "'+str(child)+'")\n'
            'include("'+str(root/'cmake/CpktDependencyContract.cmake')+'")\n'
            'cpkt_prepare_dependency_component(NAME toy BUILD_ROOT "'+str(root/'.cache/deps-build'/self.target/'toy')+'" INSTALL_ROOT "'+str(root/'.cache/deps'/self.target/'toy/install')+'")\n')
        return script

    def test_native_refresh_preflights_contract_evidence_and_roots(self):
        for kind in ('group','target','verification','receipt','dangling-receipt','readiness','contract','dangling-contract','contract-parent','dangling-contract-parent','build-root','install-root','caller-owned'):
            with self.subTest(kind=kind),tempfile.TemporaryDirectory(dir=ROOT/'build') as tmp:
                root,external,sentinel,child,env=self.fixture(Path(tmp))
                script=self.toy_contract(root,child)
                build=root/'.cache/deps-build'/self.target/'toy'; build.mkdir(parents=True)
                install=root/'.cache/deps'/self.target/'toy/install'; install.mkdir(parents=True)
                (build/'keep').write_bytes(b'build'); (install/'keep').write_bytes(b'install')
                evidence=root/'build/verification'/self.target/REPOSITORY_GROUP
                contract=root/'.cache/dependency-contracts'/self.target/'toy.txt'
                links={'group':evidence,'target':evidence.parent,'verification':evidence.parent.parent,
                    'receipt':evidence/'component-toy.json','dangling-receipt':evidence/'component-toy.json',
                    'readiness':evidence/'Release-development.json','contract':contract,'dangling-contract':contract,
                    'contract-parent':contract.parent,'dangling-contract-parent':contract.parent,
                    'build-root':build,'install-root':install}
                if kind=='caller-owned':
                    script.write_text(script.read_text().replace('set(CPKT_EXTERNAL_ROOT_LIFECYCLE_OWNED ON)','set(CPKT_EXTERNAL_ROOT_LIFECYCLE_OWNED OFF)'))
                    link=None
                else:
                    link=links[kind]
                    if kind in ('build-root','install-root'):shutil.rmtree(link)
                    link.parent.mkdir(parents=True,exist_ok=True)
                    directory=kind in ('group','target','verification','contract-parent','dangling-contract-parent','build-root','install-root')
                    link.symlink_to(external/'absent' if kind.startswith('dangling') else external if directory else sentinel,target_is_directory=directory)
                # Sibling evidence is already present when a later validation fails.
                sibling=(external if kind in ('group','target','verification') else evidence)/'Debug-development.json'
                sibling.parent.mkdir(parents=True,exist_ok=True); sibling.write_bytes(b'sibling evidence\n'); sibling.chmod(0o600)
                before_build=None if kind=='build-root' else self.identity(build/'keep')
                before_install=None if kind=='install-root' else self.identity(install/'keep')
                result=self.operation(root,env,root,['cmake','-P',script])
                if link:self.refuse(result,root,sentinel,link,sibling)
                else:
                    self.assertNotEqual(0,result.returncode); self.assertIn('caller-owned roots',result.stderr)
                    self.assertEqual(b'sibling evidence\n',sibling.read_bytes())
                if before_build:self.assertEqual(before_build,self.identity(build/'keep'))
                if before_install:self.assertEqual(before_install,self.identity(install/'keep'))
                self.assertFalse((external/'absent').exists())

    def test_native_contract_cold_and_warm(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as tmp:
            root,external,sentinel,child,env=self.fixture(Path(tmp)); script=self.toy_contract(root,child)
            # Lifecycle-owned roots retain valid relative parent segments.
            script.write_text(script.read_text().replace('/.cache/deps-build/','/build/../.cache/deps-build/'))
            evidence=root/'build/verification'/self.target/REPOSITORY_GROUP; evidence.mkdir(parents=True)
            for name in ('Debug-development.json','component-toy.json','component-other.json','Debug-built.json'):(evidence/name).write_text('old')
            result=self.operation(root,env,root,['cmake','-P',script]); self.assertEqual(0,result.returncode,result.stderr)
            self.assertEqual({'component-other.json','Debug-built.json'},{p.name for p in evidence.iterdir()})
            contract=root/'.cache/dependency-contracts'/self.target/'toy.txt'; before=self.identity(contract)
            install=root/'.cache/deps'/self.target/'toy/install'; install.mkdir(parents=True); (install/'keep').write_text('warm output')
            result=self.operation(root,env,root,['cmake','-P',script]); self.assertEqual(0,result.returncode,result.stderr)
            self.assertEqual(before,self.identity(contract)); self.assertEqual('warm output',(install/'keep').read_text())
            self.assertTrue((root/'child-marker').exists())

    def test_public_deps_and_direct_producer_cache_refuse_redirects(self):
        from native_lifecycle_fixture import capture
        for route in ('public','direct'):
            for kind in ('leaf','dangling','parent','missing-parent'):
                with self.subTest(route=route,kind=kind),tempfile.TemporaryDirectory(dir=ROOT/'build') as tmp:
                    root=Path(tmp)/'repo'; env,calls=capture(root)
                    external=Path(tmp)/'foreign'; external.mkdir(); sentinel=external/'sentinel'; sentinel.write_bytes(b'foreign bytes\n'); sentinel.chmod(0o640)
                    producer=root/'build'/self.target/REPOSITORY_GROUP/'producer'; cache=producer/'CMakeCache.txt'
                    link=cache if kind in ('leaf','dangling') else producer
                    link.parent.mkdir(parents=True,exist_ok=True)
                    link.symlink_to(external/'absent' if kind in ('dangling','missing-parent') else sentinel if kind=='leaf' else external,target_is_directory=kind in ('parent','missing-parent'))
                    args=['bash',root/'scripts/build.sh','deps','--group',REPOSITORY_GROUP,'--preset','debug'] if route=='public' else ['cmake','-DCPKT_PRODUCER='+str(producer),'-DCPKT_CONSUMER='+str(root/'consumer'),'-P',root/'cmake/producer-cache.cmake']
                    result=self.operation(root,env,root,args) if route=='direct' else subprocess.run(list(map(str,args)),cwd=root,env=env,capture_output=True,text=True)
                    self.refuse(result,root,sentinel,link)
                    self.assertFalse(calls.exists()); self.assertFalse((external/'absent').exists())

    def test_public_producer_flags_update_remove_and_cold_across_generators(self):
        from native_lifecycle_fixture import capture
        keys=('CMAKE_C_FLAGS','CMAKE_CXX_FLAGS','CMAKE_EXE_LINKER_FLAGS','CMAKE_SHARED_LINKER_FLAGS','CMAKE_MODULE_LINKER_FLAGS','CMAKE_STATIC_LINKER_FLAGS')
        for generator in ('Ninja','Unix Makefiles'):
            with self.subTest(generator=generator),tempfile.TemporaryDirectory(dir=ROOT/'build') as tmp:
                root=Path(tmp)/'repo'; env,calls=capture(root); env['CPKT_DEPENDENCY_BUILD_JOBS']='2'
                producer=root/'build'/self.target/REPOSITORY_GROUP/'producer'; consumer=producer.parent/'Debug'; consumer.mkdir(parents=True)
                producer.mkdir(); (root/'CMakePresets.json').write_text(json.dumps({'version':3,'configurePresets':[{'name':'debug','generator':generator,'binaryDir':str(producer)}]}))
                wrapper=Path(env['CMAKE']); wrapper.write_text(wrapper.read_text()+'raise SystemExit(subprocess.call(['+repr(shutil.which('cmake'))+',*a]))\n')
                (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(toy NONE)\nadd_custom_target(cpkt_deps_all)\n')
                # Public deps executes real cache synchronization and native toy configure/build.
                def public():
                    result=subprocess.run(['bash',str(root/'scripts/build.sh'),'deps','--group',REPOSITORY_GROUP,'--preset','debug'],cwd=root,env=env,capture_output=True,text=True)
                    self.assertEqual(0,result.returncode,result.stdout+result.stderr)
                    recorded=[json.loads(line) for line in calls.read_text().splitlines()]
                    self.assertEqual(2,len(recorded)); self.assertIn('-DCPKT_DEPENDENCY_PRODUCER=ON',recorded[0]['args'])
                    self.assertIn('-DCPKT_DEPENDENCY_BUILD_JOBS=2',recorded[0]['args'])
                    self.assertEqual(['--build',str(producer),'--parallel','2','--target','cpkt_deps_all'],recorded[1]['args']); calls.unlink()
                public(); self.assertTrue((producer/'CMakeCache.txt').is_file())
                result=subprocess.run(['cmake','-S',str(root),'-B',str(producer),'-G',generator],capture_output=True,text=True); self.assertEqual(0,result.returncode,result.stderr)
                cache=producer/'CMakeCache.txt'
                cache.write_text(cache.read_text()+''.join('//old flag comment\n'+key+':STRING=old\n\n' for key in keys))
                (consumer/'CMakeCache.txt').write_text(''.join('//selected\n'+key+':STRING=new flag;literal\n' for key in keys))
                public()
                for key in keys:self.assertIn(key+':STRING=new flag;literal\n',cache.read_text())
                (consumer/'CMakeCache.txt').unlink(); public()
                for key in keys:self.assertNotIn(key+':',cache.read_text())
                self.assertNotIn('//old flag comment',cache.read_text())
                result=subprocess.run(['cmake','-S',str(root),'-B',str(producer),'-G',generator],capture_output=True,text=True); self.assertEqual(0,result.returncode,result.stdout+result.stderr)

    def test_native_coverage_and_sdk_writers_preflight_all_leaves(self):
        for surface in ('coverage','sdk-json','sdk-script'):
            for kind in ('leaf','dangling','parent'):
                with self.subTest(surface=surface,kind=kind),tempfile.TemporaryDirectory(dir=ROOT/'build') as tmp:
                    root,external,sentinel,child,env=self.fixture(Path(tmp)); graph=root/'build/graph'
                    name='cpkt-required-coverage.txt' if surface=='coverage' else 'cpkt-package-components.json' if surface=='sdk-json' else 'cpkt-sdk-install.cmake'
                    leaf=graph/name; link=graph if kind=='parent' else leaf
                    link.parent.mkdir(parents=True,exist_ok=True); link.symlink_to(external if kind=='parent' else external/'absent' if kind=='dangling' else sentinel,target_is_directory=kind=='parent')
                    script=root/'writer.cmake'; script.write_text('cmake_minimum_required(VERSION 3.21)\nset(CMAKE_BINARY_DIR "'+str(graph)+'")\nset(CMAKE_SOURCE_DIR "'+str(root)+'")\nset(CPKT_GROUP '+REPOSITORY_GROUP+')\nset(CMAKE_BUILD_TYPE Release)\nset(CPKT_INVENTORY [=[{"tests":{},"groups":{"'+REPOSITORY_GROUP+'":{"package":{}}}}]=])\ninclude("'+str(root/'cmake'/('CpktTestInventory.cmake' if surface=='coverage' else 'CpktSDKInstall.cmake'))+'")\n'+('cpkt_assert_test_inventory()' if surface=='coverage' else 'cpkt_register_sdk_install("")')+'\n')
                    result=self.operation(root,env,root,['cmake','-P',script]); self.refuse(result,root,sentinel,link)
                    if surface=='sdk-script':self.assertFalse((graph/'cpkt-package-components.json').exists())

    def test_imported_core_aliases_validate_all_parents_before_linking(self):
        for kind in ('safe','parent','dangling-parent'):
            with self.subTest(kind=kind),tempfile.TemporaryDirectory(dir=ROOT/'build') as tmp:
                root,external,sentinel,child,env=self.fixture(Path(tmp))
                inventory={'schema_version':1,'repository_group':REPOSITORY_GROUP,'groups':{REPOSITORY_GROUP:{'requires':[]}},'components':{'a':{'external':'cpkt','directory':'a'},'b':{'external':'cpkt','directory':'b'}}}
                (root/'cmake/components.json').write_text(json.dumps(inventory))
                prefix=root/'.cache/cpkt'/self.target/'install'; prefix.mkdir(parents=True); (prefix/'header').write_text('immutable SDK')
                base=root/'.cache/deps'/self.target; base.mkdir(parents=True)
                link=base/'b'
                if kind!='safe':link.symlink_to(external if kind=='parent' else external/'absent',target_is_directory=True)
                args=['cmake','-DCPKT_REPO_ROOT='+str(root),'-DCPKT_CORE_TARGET='+self.target,'-DCPKT_CORE_ACTION=link','-P',root/'cmake/core-dependency.cmake']
                result=self.operation(root,env,root,args)
                if kind!='safe':
                    self.refuse(result,root,sentinel,link); self.assertFalse((base/'a').exists())
                else:
                    self.assertEqual(0,result.returncode,result.stderr)
                    self.assertEqual(prefix,(base/'a/install').resolve())
                    before=self.identity(prefix/'header')
                    result=self.operation(root,env,root,args); self.assertEqual(0,result.returncode,result.stderr)
                    self.assertEqual(before,self.identity(prefix/'header'))

    def test_core_acquisition_path_refused_before_pin_or_archive_work(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as tmp:
            root,external,sentinel,child,env=self.fixture(Path(tmp)); link=root/'build/core-acquisition'/self.target/'archive-path'
            link.parent.mkdir(parents=True); link.symlink_to(sentinel)
            result=self.operation(root,env,root,['cmake','-DCPKT_REPO_ROOT='+str(root),'-DCPKT_CORE_TARGET='+self.target,'-DCPKT_CORE_ACTION=acquire','-DCPKT_CORE_PIN='+str(root/'absent-pin'),'-P',root/'cmake/core-dependency.cmake'])
            self.refuse(result,root,sentinel,link); self.assertFalse((root/'.cache').exists())


class VerificationMutation(unittest.TestCase):
    """Exercise public metadata surfaces in isolated, acquisition-free repositories."""
    target = 'x86_64-linux-gnu'
    provider = {'core': 'cpkt', 'db': 'cpktdb', 'misc': 'cpktmisc'}[REPOSITORY_GROUP]

    def fixture(self, work):
        from native_lifecycle_fixture import seed, environment
        root = work/'repo'
        seed(root)
        external = work/'unrelated'
        external.mkdir()
        sentinel = external/'sentinel'
        sentinel.write_bytes(b'unrelated bytes\n')
        sentinel.chmod(0o640)
        return root, external, sentinel, environment()

    def redirect(self, path, kind, external, sentinel):
        link = path.parent if kind == 'parent' else path
        link.parent.mkdir(parents=True, exist_ok=True)
        destination = external if kind == 'parent' else sentinel
        if kind == 'dangling':
            destination = external/'absent'
        link.symlink_to(destination, target_is_directory=kind == 'parent')
        return link

    def assert_refused(self, result, external, sentinel, link, root):
        self.assertNotEqual(0, result.returncode, result.stdout+result.stderr)
        self.assertIn('symlink ancestor', result.stderr)
        self.assertEqual(b'unrelated bytes\n', sentinel.read_bytes())
        self.assertEqual(0o640, sentinel.stat().st_mode & 0o777)
        self.assertEqual([sentinel], list(external.iterdir()))
        self.assertTrue(link.is_symlink())
        self.assertFalse((root/'child-marker').exists())
        self.assertFalse((root/'.cache').exists())

    def operation(self, root, env, arguments, group=None):
        return subprocess.run(['bash', str(root/'scripts/operation.sh'), '--group', group or REPOSITORY_GROUP,
                               '--', *map(str, arguments)], cwd=root, env=env, capture_output=True, text=True)

    def evidence(self, root, script, action, configuration='Debug'):
        return [sys.executable, root/'scripts'/script, action, '--root', root, '--group', REPOSITORY_GROUP,
                '--target', self.target, '--configuration', configuration, '--preset', 'debug']

    def test_public_checksum_outputs_reject_ancestry_and_temporary_redirects(self):
        for scope in ('selected', 'binary', 'release'):
            for kind in ('leaf', 'parent', 'dangling', 'temporary'):
                with self.subTest(scope=scope, kind=kind), tempfile.TemporaryDirectory(
                        prefix='metadata-checksums-', dir=ROOT/'build') as temporary:
                    root, external, sentinel, env = self.fixture(Path(temporary))
                    name = self.provider+'-1.2.3-'+self.target+'.tar.gz'
                    archive = root/'build/package-stage'/self.target/REPOSITORY_GROUP/'archives'/name
                    archive.parent.mkdir(parents=True)
                    archive.write_bytes(b'fixture archive')
                    output = (root/'build/verification'/self.target/REPOSITORY_GROUP/'CHECKSUMS' if scope == 'selected'
                              else root/'dist'/(self.provider+'-1.2.3-CHECKSUMS') if scope == 'release'
                              else root/'build/verification/binary/1.2.3/CHECKSUMS')
                    path = output.with_suffix('.tmp') if kind == 'temporary' else output
                    link = self.redirect(path, kind, external, sentinel)
                    old = root/'build/verification'/scope/'1.2.3/proof.json'
                    if not (scope == 'binary' and kind == 'parent'):
                        old.parent.mkdir(parents=True, exist_ok=True)
                        old.write_bytes(b'previous aggregate evidence')
                    arguments = ['bash', str(root/'scripts/package.sh'), 'package-checksums',
                                 '--group', REPOSITORY_GROUP if scope == 'selected' else 'all',
                                 '--scope', scope]
                    if scope == 'selected':
                        arguments += ['--preset', 'release']
                    result = subprocess.run(arguments, cwd=root, env=env, capture_output=True, text=True)
                    self.assert_refused(result, external, sentinel, link, root)
                    if not (scope == 'binary' and kind == 'parent'):
                        self.assertEqual(b'previous aggregate evidence', old.read_bytes())

    def test_public_selected_checksums_are_exact_and_missing_inputs_do_not_publish(self):
        with tempfile.TemporaryDirectory(prefix='metadata-checksum-success-', dir=ROOT/'build') as temporary:
            root, _, _, env = self.fixture(Path(temporary))
            name = self.provider+'-1.2.3-'+self.target+'.tar.gz'
            archive = root/'build/package-stage'/self.target/REPOSITORY_GROUP/'archives'/name
            archive.parent.mkdir(parents=True)
            archive.write_bytes(b'fixture archive')
            output = root/'build/verification'/self.target/REPOSITORY_GROUP/'CHECKSUMS'
            arguments = ['bash', str(root/'scripts/package.sh'), 'package-checksums', '--group', REPOSITORY_GROUP,
                         '--preset', 'release', '--scope', 'selected']
            result = subprocess.run(arguments, cwd=root, env=env, capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stdout+result.stderr)
            expected = sha(b'fixture archive')+'  '+name+'\n'
            self.assertEqual(expected.encode(), output.read_bytes())
            mask = os.umask(0)
            os.umask(mask)
            self.assertEqual(0o666 & ~mask, output.stat().st_mode & 0o777)
            self.assertFalse(output.with_suffix('.tmp').exists())
            output.unlink()
            archive.unlink()
            for dangling in (False, True):
                if dangling:
                    archive.symlink_to(root/'absent-input')
                result = subprocess.run(arguments, cwd=root, env=env, capture_output=True, text=True)
                self.assertNotEqual(0, result.returncode)
                self.assertFalse(output.exists())
                self.assertFalse(output.with_suffix('.tmp').exists())
                self.assertFalse((root/'.cache').exists())

    def test_build_and_memcheck_evidence_refuse_redirects_before_invalidation(self):
        for script, actions, suffix in (
                ('cpkt_build_evidence.py', ('before', 'built', 'restore', 'inventory', 'tested'), '-development.json'),
                ('cpkt_memcheck_evidence.py', ('inventory', 'tested'), '-memcheck.json')):
            for action in actions:
                for kind in ('leaf', 'parent', 'dangling'):
                    with self.subTest(script=script, action=action, kind=kind), tempfile.TemporaryDirectory(
                            prefix='metadata-evidence-', dir=ROOT/'build') as temporary:
                        root, external, sentinel, env = self.fixture(Path(temporary))
                        output = root/'build/verification'/self.target/REPOSITORY_GROUP/('Debug'+suffix)
                        link = self.redirect(output, kind, external, sentinel)
                        result = self.operation(root, env, self.evidence(root, script, action))
                        self.assert_refused(result, external, sentinel, link, root)
                        self.assertFalse(list((root/'build/control').glob('readiness/*')))

    def test_current_run_and_built_paths_are_checked_before_revoking_ready(self):
        for location in ('run', 'built'):
            for kind in ('leaf', 'parent', 'dangling'):
                with self.subTest(location=location, kind=kind), tempfile.TemporaryDirectory(
                        prefix='metadata-current-run-', dir=ROOT/'build') as temporary:
                    root, external, sentinel, env = self.fixture(Path(temporary))
                    ready = root/'build/verification'/self.target/REPOSITORY_GROUP/'Debug-development.json'
                    ready.parent.mkdir(parents=True)
                    ready.write_bytes(b'previous readiness')
                    ready.chmod(0o640)
                    command = self.evidence(root, 'cpkt_build_evidence.py', 'before')
                    driver = root/'driver.py'
                    relative = ('build/control/readiness' if location == 'run' else
                                'build/verification/'+self.target+'/'+REPOSITORY_GROUP)
                    driver.write_text('import os\nfrom pathlib import Path\n'
                        'path=Path('+repr(str(root/relative))+')\n'+
                        ('path=path/os.environ["CPKT_OPERATION_RUN"]/'+repr(self.target)+'/'+repr(REPOSITORY_GROUP)+'/"Debug.json"\n'
                         if location == 'run' else 'path=path/"Debug-built.json"\n')+
                        ('path=path.parent\n' if kind == 'parent' else '')+
                        'path.parent.mkdir(parents=True,exist_ok=True)\n'
                        'path.symlink_to('+repr(str(external if kind == 'parent' else external/'absent' if kind == 'dangling' else sentinel))+')\n'
                        'Path('+repr(str(root/'redirect-path'))+').write_text(str(path))\n'
                        'os.execv('+repr(sys.executable)+','+repr(list(map(str, command)))+')\n')
                    # A parent redirect for built evidence also redirects ready; set
                    # it up first and preserve the original ready in the private fixture.
                    if location == 'built' and kind == 'parent':
                        ready.unlink()
                        ready.parent.rmdir()
                    result = self.operation(root, env, [sys.executable, driver])
                    self.assert_refused(result, external, sentinel, Path((root/'redirect-path').read_text()), root)
                    if not (location == 'built' and kind == 'parent'):
                        self.assertEqual(b'previous readiness', ready.read_bytes())
                        self.assertEqual(0o640, ready.stat().st_mode & 0o777)

    def test_selected_and_source_invalidation_and_configure_preserve_unrelated_evidence(self):
        for surface in ('selected', 'source', 'configure', 'release'):
            for kind in ('leaf', 'parent', 'dangling'):
                with self.subTest(surface=surface, kind=kind), tempfile.TemporaryDirectory(
                        prefix='metadata-invalidation-', dir=ROOT/'build') as temporary:
                    root, external, sentinel, env = self.fixture(Path(temporary))
                    output = (root/'build/verification'/self.target/REPOSITORY_GROUP/'package-ready.json'
                              if surface == 'selected' else root/'build/verification/source/1.2.3/proof.json'
                              if surface == 'source' else root/'build/verification'/self.target/REPOSITORY_GROUP/'Debug-development.json'
                              if surface == 'configure' else root/'build/verification/release/1.2.3/proof.json')
                    link = self.redirect(output, kind, external, sentinel)
                    manifest = root/'dist'/(self.provider+'-1.2.3-CHECKSUMS')
                    manifest.parent.mkdir()
                    manifest.write_bytes(b'previous release checksums')
                    if surface == 'selected':
                        args = [sys.executable, root/'scripts/cpkt_packages.py', 'invalidate-selected', '--group', REPOSITORY_GROUP, '--preset', 'release']
                    elif surface == 'source':
                        args = [sys.executable, root/'scripts/cpkt_source_proof.py', '--invalidate', '1.2.3']
                    elif surface == 'configure':
                        args = [sys.executable, root/'scripts/cpkt_configure_guard.py', '--root', root, '--group', REPOSITORY_GROUP,
                                '--binary', root/'build'/self.target/REPOSITORY_GROUP/'Debug', '--target', self.target]
                    else:
                        args = [sys.executable, root/'scripts/cpkt_packages.py', 'invalidate', '--group', 'all', '--version', '1.2.3']
                    result = self.operation(root, env, args, group='all' if surface in ('source', 'release') else None)
                    self.assert_refused(result, external, sentinel, link, root)
                    self.assertEqual(b'previous release checksums', manifest.read_bytes())

    def test_helpers_refuse_proof_and_output_redirects_before_child_execution(self):
        for location in ('helper', 'clangd', 'output'):
            for dangling in (False, True):
                with self.subTest(location=location, dangling=dangling), tempfile.TemporaryDirectory(
                        prefix='metadata-helper-', dir=ROOT/'build') as temporary:
                    root, external, sentinel, env = self.fixture(Path(temporary))
                    graph = root/'build'/self.target/REPOSITORY_GROUP/'Debug'
                    output = root/'build/fixture-output'
                    path = root/'build/control/helper-proofs' if location == 'helper' else graph/'clangd-proofs' if location == 'clangd' else output
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.symlink_to(external/'absent' if dangling else external, target_is_directory=True)
                    args = ['bash', root/'scripts/helper.sh', '--root', root, '--group', REPOSITORY_GROUP,
                            '--mode', 'clangd' if location == 'clangd' else 'fixture']
                    if location == 'clangd':
                        args += ['--owned-build', graph]
                    if location == 'output':
                        args += ['--output', output]
                    args += ['--', sys.executable, '-c', 'from pathlib import Path;Path('+repr(str(root/'child-marker'))+').touch()']
                    result = self.operation(root, env, args)
                    self.assert_refused(result, external, sentinel, path, root)

    def test_missing_and_dangling_helper_inputs_do_not_execute(self):
        for dangling in (False, True):
            with self.subTest(dangling=dangling), tempfile.TemporaryDirectory(prefix='metadata-helper-input-', dir=ROOT/'build') as temporary:
                root, _, _, env = self.fixture(Path(temporary))
                path = root/'input'
                if dangling:
                    path.symlink_to(root/'absent')
                args = ['bash', root/'scripts/helper.sh', '--root', root, '--group', REPOSITORY_GROUP,
                        '--mode', 'fixture', '--input', path, '--', sys.executable, '-c',
                        'from pathlib import Path;Path('+repr(str(root/'child-marker'))+').touch()']
                result = self.operation(root, env, args)
                self.assertNotEqual(0, result.returncode)
                self.assertIn('input missing/corrupt', result.stderr)
                self.assertFalse((root/'child-marker').exists())
                self.assertFalse((root/'build/control/helper-proofs').exists())

    def test_helper_final_proof_redirect_is_refused_before_reuse_or_stale_unlink(self):
        for mode in ('fixture', 'clangd'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(prefix='metadata-helper-leaf-', dir=ROOT/'build') as temporary:
                root, external, sentinel, env = self.fixture(Path(temporary))
                graph = root/'build'/self.target/REPOSITORY_GROUP/'Debug'
                graph.mkdir(parents=True)
                options = ['--root', str(root), '--group', REPOSITORY_GROUP, '--mode', mode]
                if mode == 'clangd':
                    options += ['--owned-build', str(graph)]
                options += ['--', sys.executable, '-c', 'from pathlib import Path;Path('+repr(str(root/'child-marker'))+').touch()']
                base = graph/'clangd-proofs' if mode == 'clangd' else root/'build/control/helper-proofs'
                driver = root/'leaf.py'
                driver.write_text('import os,subprocess\nfrom pathlib import Path\n'
                    'fds=tuple(int(os.environ[k]) for k in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"))\n'
                    'subprocess.run('+repr([sys.executable, str(root/'scripts/cpkt_helper_proof.py'), '--publish', *options])+',check=True,pass_fds=fds)\n'
                    'proof=next(Path('+repr(str(base))+').rglob("*.json"))\n'
                    'proof.unlink();proof.symlink_to('+repr(str(sentinel))+')\n'
                    'Path('+repr(str(root/'redirect-path'))+').write_text(str(proof))\n'
                    'os.execv("/bin/bash",'+repr(['bash', str(root/'scripts/helper.sh'), *options])+')\n')
                result = self.operation(root, env, [sys.executable, driver])
                self.assert_refused(result, external, sentinel, Path((root/'redirect-path').read_text()), root)

    def test_related_output_guards_precede_native_probes_or_acquisition(self):
        cases = [('package', 'package-ready.json.tmp'), ('package', 'consumer-evidence.json'),
                 ('package', 'consumer-evidence.json.tmp'), ('consumer', 'installed-consumers'),
                 ('clangd', 'clangd-proofs'), ('manifest', 'THIRD_PARTY_NOTICES.md'),
                 ('manifest', 'README.md'), ('darwin', 'darwin-source-evidence.json'),
                 ('darwin', 'darwin-artifact-input-evidence.json.tmp'), ('darwin', 'darwin-artifact-evidence.json'),
                 ('smoke', ''), ('smoke', 'darwin-smoke-test'), ('smoke', 'sdk'),
                 ('smoke-archive', self.provider+'-1.2.3-arm64-apple-darwin-smoke-test.zip'),
                 ('sdk-smoke', 'darwin-artifact-smoke')]
        for surface, name in cases:
            for dangling in (False, True):
                with self.subTest(surface=surface, name=name, dangling=dangling), tempfile.TemporaryDirectory(
                        prefix='metadata-related-', dir=ROOT/'build') as temporary:
                    root, external, sentinel, env = self.fixture(Path(temporary))
                    prefix = root/'build/prefix'
                    graph = root/'build'/self.target/REPOSITORY_GROUP/'Debug'
                    path = (root/'build/package-stage/arm64-apple-darwin/all/smoke'/name if surface == 'smoke'
                            else root/'dist'/name if surface == 'smoke-archive'
                            else graph/name if surface == 'clangd' else prefix/'share/doc/cpkt'/REPOSITORY_GROUP/name
                            if surface == 'manifest' else root/'build'/name if surface in ('darwin', 'sdk-smoke')
                            else root/'build/verification'/self.target/REPOSITORY_GROUP/name)
                    link = self.redirect(path, 'dangling' if dangling else 'leaf', external, sentinel)
                    if surface == 'package':
                        args = [sys.executable, root/'scripts/cpkt_packages.py', 'verify-selected', '--preset', 'release', '--group', REPOSITORY_GROUP]
                    elif surface == 'consumer':
                        args = [sys.executable, root/'scripts/cpkt_sdk_consumer.py', '--prefix', prefix, '--target', self.target,
                                '--preset', 'release', '--groups', 'core' if REPOSITORY_GROUP == 'core' else 'core,'+REPOSITORY_GROUP,
                                '--owners', REPOSITORY_GROUP]
                    elif surface == 'clangd':
                        args = [sys.executable, root/'scripts/cpkt_clangd_check.py', '--root', root, '--build', graph,
                                '--group', REPOSITORY_GROUP, '--source', root/'input.c', '--checker', '/bin/true', '--gate', root/'gate']
                    elif surface == 'manifest':
                        args = [sys.executable, root/'scripts/cpkt_package_manifest.py', '--root', root, '--group', REPOSITORY_GROUP,
                                '--prefix', prefix, '--target', self.target, '--preset', 'release', '--version', '1.2.3']
                    elif surface in ('smoke', 'smoke-archive', 'sdk-smoke'):
                        args = [sys.executable, root/'scripts/cpkt_darwin.py', 'sdk-smoke' if surface == 'sdk-smoke' else 'smoke-zip',
                                '--version', '1.2.3']
                    else:
                        action = {'darwin-source-evidence.json': 'source-evidence',
                                  'darwin-artifact-input-evidence.json.tmp': 'sdk-input',
                                  'darwin-artifact-evidence.json': 'sdk-evidence'}[name]
                        args = [sys.executable, root/'scripts/cpkt_darwin.py', action]
                    result = self.operation(root, env, args, group='all' if surface in ('darwin', 'smoke', 'smoke-archive', 'sdk-smoke') else None)
                    self.assert_refused(result, external, sentinel, link, root)

    def test_handoff_output_preflight_prevents_acquisition(self):
        for kind in ('leaf', 'parent', 'dangling', 'temporary'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory(prefix='metadata-handoff-', dir=ROOT/'build') as temporary:
                root, external, sentinel, env = self.fixture(Path(temporary))
                destination = root/'build/download'
                path = destination/('handoff.json.tmp' if kind == 'temporary' else 'handoff.json')
                link = self.redirect(path, kind, external, sentinel)
                driver = root/'handoff.py'
                driver.write_text('import sys\nfrom pathlib import Path\nfrom unittest.mock import patch\n'
                    'sys.path.insert(0,'+repr(str(root/'scripts'))+')\n'
                    'import cpkt_github_handoff as h\nfrom cpkt_packages import artifacts\n'
                    'names=artifacts("1.2.3","release")+['+repr(self.provider+'-1.2.3-CHECKSUMS')+']\n'
                    'value=dict(schema_version=1,repository=h.REPOSITORY,producer_commit="a"*40,tag="v1.2.3",version="1.2.3",manifest_sha256="b"*64,draft_id=99,assets={n:dict(id=i+1,size=3,sha256="b"*64) for i,n in enumerate(names)})\n'
                    'def acquire(*args):\n  Path('+repr(str(root/'child-marker'))+').touch()\n  raise AssertionError("acquisition attempted")\n'
                    'with patch.object(h,"acquire",side_effect=acquire):\n'
                    '  h.download_handoff(None,value,Path('+repr(str(root/'shared-cache'))+'),Path('+repr(str(destination))+'))\n')
                result = self.operation(root, env, [sys.executable, driver])
                self.assert_refused(result, external, sentinel, link, root)
                self.assertFalse((root/'shared-cache').exists())

    def test_json_redirects_bytes_modes_and_atomic_failure_cleanup(self):
        for writer in ('packages', 'receipts'):
            kinds = ('leaf', 'parent', 'dangling', 'temporary') if writer == 'packages' else ('leaf', 'parent', 'dangling')
            for kind in kinds:
                with self.subTest(writer=writer, kind=kind), tempfile.TemporaryDirectory(prefix='metadata-json-', dir=ROOT/'build') as temporary:
                    root, external, sentinel, env = self.fixture(Path(temporary))
                    output = root/'build/verification/json/proof.json'
                    path = output.with_name(output.name+'.tmp') if kind == 'temporary' else output
                    link = self.redirect(path, kind, external, sentinel)
                    if kind == 'temporary':
                        output.write_bytes(b'previous JSON')
                        output.chmod(0o640)
                    driver = root/'writer.py'
                    driver.write_text('import sys\nsys.path.insert(0,'+repr(str(root/'scripts'))+')\n'+
                        ('from cpkt_packages import write_json as write\n' if writer == 'packages' else
                         'from cpkt_receipts import publish as write\n')+
                        'from pathlib import Path\nwrite(Path('+repr(str(output))+'),{"z":1,"a":"value"})\n')
                    result = self.operation(root, env, [sys.executable, driver])
                    self.assert_refused(result, external, sentinel, link, root)
                    if kind == 'temporary':
                        self.assertEqual(b'previous JSON', output.read_bytes())
                        self.assertEqual(0o640, output.stat().st_mode & 0o777)
            with tempfile.TemporaryDirectory(prefix='metadata-json-atomic-', dir=ROOT/'build') as temporary:
                root, _, _, env = self.fixture(Path(temporary))
                driver = root/'atomic.py'
                driver.write_text('import sys,os,json\nfrom pathlib import Path\nfrom unittest.mock import patch\n'
                    'sys.path.insert(0,'+repr(str(root/'scripts'))+')\n'
                    'from cpkt_packages import write_json\nfrom cpkt_receipts import publish\n'
                    'p=Path('+repr(str(root/'build/verification/atomic/proof.json'))+')\n'
                    'write='+('write_json' if writer == 'packages' else 'publish')+'\n'
                    'expected={"z":1,"a":"value"}\n'
                    + ('' if writer == 'packages' else 'expected.update(schema_version=1,status="passed",run=os.environ["CPKT_OPERATION_RUN"])\n')+
                    'write(p,{"z":1,"a":"value"})\n'
                    'assert p.read_bytes()==json.dumps(expected,sort_keys=True,separators=(",",":")).encode()\n'
                    'assert p.stat().st_mode & 0o777 == '+('0o644' if writer == 'packages' else '0o600')+'\n'
                    'old=p.read_bytes();p.chmod(0o640)\n'
                    'with patch('+repr('pathlib.Path.replace' if writer == 'packages' else 'cpkt_receipts.os.replace')+',side_effect=OSError("publication refused")):\n'
                    '  try:write(p,{"different":True})\n'
                    '  except OSError:pass\n'
                    '  else:raise AssertionError("publication unexpectedly succeeded")\n'
                    'assert p.read_bytes()==old and p.stat().st_mode & 0o777 == 0o640\n'
                    'assert list(p.parent.iterdir())==[p]\n')
                result = self.operation(root, env, [sys.executable, driver])
                self.assertEqual(0, result.returncode, result.stdout+result.stderr)

    def test_successful_build_restore_and_memcheck_keep_exact_coverage_and_modes(self):
        with tempfile.TemporaryDirectory(prefix='metadata-ready-success-', dir=ROOT/'build') as temporary:
            root, _, _, env = self.fixture(Path(temporary))
            data = {'schema_version': 1, 'repository_group': REPOSITORY_GROUP,
                    'groups': {REPOSITORY_GROUP: {'requires': [] if REPOSITORY_GROUP == 'core' else ['core']}},
                    'components': {}, 'targets': {}, 'tests': {'fixture': {'group': REPOSITORY_GROUP}}}
            (root/'cmake/components.json').write_text(json.dumps(data))
            (root/'CMakeLists.txt').write_text('# synthetic native graph\n')
            graph = root/'build'/self.target/REPOSITORY_GROUP/'Debug'
            graph.mkdir(parents=True)
            (graph/'CMakeCache.txt').write_text('CPKT_TARGET_ID:STRING='+self.target+'\nCMAKE_BUILD_TYPE:STRING=Debug\n')
            output = graph/'output'
            output.write_bytes(b'compiled fixture output')
            (graph/'cpkt-owned-outputs.txt').write_text(str(output)+'\n')
            (graph/'cpkt-required-coverage.txt').write_text('fixture\n')
            listing = json.dumps({'tests': [{'name': 'fixture', 'command': ['/bin/true']}]})
            for kind in ('test', 'memcheck'):
                (graph/('cpkt-'+kind+'-inventory.json')).write_text(listing)
                (graph/('cpkt-'+kind+'-results.xml')).write_text('<testsuite><testcase name="fixture"/></testsuite>')
            (graph/'Testing/tag').mkdir(parents=True)
            (graph/'Testing/TAG').write_text('tag\n')
            (graph/'Testing/tag/DynamicAnalysis.xml').write_text('<Site><Defect>0</Defect></Site>')
            tools = root/'tools'
            tools.mkdir()
            shutil.copy2('/bin/true', tools/'valgrind')
            env['PATH'] = str(tools)+os.pathsep+env['PATH']
            build = list(map(str, self.evidence(root, 'cpkt_build_evidence.py', 'tested')))
            memory = list(map(str, self.evidence(root, 'cpkt_memcheck_evidence.py', 'tested')))
            driver = root/'success.py'
            driver.write_text('import os,json,subprocess\nfrom pathlib import Path\n'
                'fds=tuple(int(os.environ[k]) for k in ("CPKT_OPERATION_FD","CPKT_OPERATION_CAP_FD"))\n'
                'build='+repr(build)+'\nmemory='+repr(memory)+'\n'
                'base=Path('+repr(str(root/'build/verification'/self.target/REPOSITORY_GROUP))+')\n'
                'ready=base/"Debug-development.json"\n'
                'def run(command):subprocess.run(command,check=True,pass_fds=fds)\n'
                'run(build);original=ready.read_bytes()\n'
                'for action in ("before","built","restore"):\n'
                '  command=build[:];command[2]=action;run(command)\n'
                '  if action=="before":assert not ready.exists()\n'
                '  else:assert ready.read_bytes()==original\n'
                'run(memory)\n'
                'whole=base/"Debug-memcheck.json";ordinary=whole.read_bytes()\n'
                'run(memory+["--regex","fixture"])\n'
                'assert whole.read_bytes()==ordinary\n'
                'assert json.loads((base/"Debug-memcheck-focused.json").read_bytes())["selection"]=="fixture"\n'
                'for p in base.glob("*.json"):\n'
                '  record=json.loads(p.read_bytes())\n'
                '  assert record["coverage"]==["fixture"] or record["kind"]=="built"\n'
                '  assert record["run"]==os.environ["CPKT_OPERATION_RUN"]\n'
                '  assert p.stat().st_mode & 0o777 == 0o600\n'
                '  assert p.read_bytes()==json.dumps(record,sort_keys=True,separators=(",",":")).encode()\n'
                'assert not list(base.glob(".receipt-*"))\n')
            result = self.operation(root, env, [sys.executable, driver])
            self.assertEqual(0, result.returncode, result.stdout+result.stderr)
            self.assertFalse((root/'.cache').exists())


if __name__=='__main__':unittest.main()
