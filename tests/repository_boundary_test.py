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

if __name__=='__main__':unittest.main()
