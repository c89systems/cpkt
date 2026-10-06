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

if __name__=='__main__':unittest.main()
