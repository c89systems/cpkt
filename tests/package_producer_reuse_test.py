#!/usr/bin/env python3
"""Exercise owning producers and immutable prerequisites with both generators."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
OWNER = json.loads((ROOT/'cmake/components.json').read_text())['repository_group']
TARGET = 'x86_64-linux-gnu'
COMPONENT = OWNER+'dep'
PHASES = [COMPONENT+':'+phase for phase in ('extract','configure','build','install')]


def invoke(root, *arguments, success=True, env=None):
    result = subprocess.run(['bash', str(root/'scripts/build.sh'), *arguments],
                            capture_output=True, text=True, env=env)
    if (result.returncode == 0) != success:
        raise RuntimeError(' '.join(arguments)+'\n'+result.stdout+result.stderr)
    return result


def prerequisite(root):
    """Install independently validated fixture bytes, without any core producer."""
    if OWNER=='core':return None
    prefix=root/'.cache/cpkt'/TARGET/'install'
    (prefix/'lib').mkdir(parents=True)
    payload=prefix/'lib/core-fixture.a';payload.write_bytes(b'immutable published core');payload.chmod(0o644)
    manifest={'schema_version':1,'group':'core','release_version':'0.1.0',
        'target_id':TARGET,'libc':'gnu','macos_deployment_target':None,
        'components':[{'name':'coredep','version':'1.0','source_sha256':'a'*64,
            'features':{'static':True},'abi':{'static_archive':'lib/core-fixture.a'}}], 'requires_core':None,
        'files':[{'path':'lib/core-fixture.a','type':'file','mode':'0644',
            'sha256':hashlib.sha256(payload.read_bytes()).hexdigest()}]}
    encoded=lambda data:json.dumps(data,sort_keys=True,separators=(',',':')).encode()
    manifest['package_id']=hashlib.sha256(encoded(manifest)).hexdigest()
    path=prefix/'share/cpkt/packages/core.json';path.parent.mkdir(parents=True)
    path.write_bytes(encoded(manifest));path.chmod(0o644)
    (root/'dependencies').mkdir()
    (root/'dependencies/cpkt.json').write_text(json.dumps({'schema_version':1,
        'repository':'c89systems/cpkt','version':'0.1.0','targets':{TARGET:{
            'url':'https://github.com/c89systems/cpkt/releases/download/v0.1.0/cpkt-0.1.0-'+TARGET+'.tar.gz',
            'sha256':'a'*64,'package_id':manifest['package_id']}}}))
    return payload


def setup(generator, parent):
    root=parent/(generator.replace(' ','-')+" checkout's files")
    for directory in ('scripts','cmake','tests'):(root/directory).mkdir(parents=True)
    for path in ROOT.glob('scripts/cpkt_*.py'):shutil.copy2(path,root/'scripts'/path.name)
    for name in ('validate-sdk.py',):shutil.copy2(ROOT/'scripts'/name,root/'scripts'/name)
    for path in ROOT.glob('scripts/*.sh'):shutil.copy2(path,root/'scripts'/path.name)
    for name in ('CpktMutationPaths.cmake','CpktGroups.cmake','CpktOperation.cmake','CpktDependencyContract.cmake','validate-dependency-contract.cmake','CpktTestInventory.cmake','CpktSDKInstall.cmake','CpktComponentInventory.cmake','CpktLiteralArguments.cmake','CpktVerifiedExternalProject.cmake','lifecycle-info.cmake','producer-cache.cmake'):
        shutil.copy2(ROOT/'cmake'/name,root/'cmake'/name)
    (root/'VERSION').write_text('1.2.3\n')
    resolver=root/'scripts/cpkt-toolchains.sh'
    resolver.write_text('#!/bin/sh\nprintf "status=ready\\nsource=synthetic-fixture\\n"\n');resolver.chmod(0o755)
    for name in ('CpktReadOnlyToolchain.cmake','CpktReadOnlyAflToolchain.cmake'):
        (root/'cmake'/name).write_text('# native fixture; no toolchain provisioning\n')
    (root/'main.c').write_text('int main(void) { return 0; }\n')
    components={COMPONENT:{'group':OWNER,'directory':COMPONENT,
        'dependencies':[] if OWNER=='core' else ['coredep'],
        'recipe_functions':['cpkt_add_'+COMPONENT],'helpers':['cpkt_add_'+COMPONENT],
        'recipe_inputs':['tests/phase.py'],'variants':['static','shared'],'payload':{}}}
    if OWNER!='core':components['coredep']={'group':'core','directory':'coredep',
        'external':'cpkt','dependencies':[],'recipe_inputs':[],'helpers':[]}
    data={'schema_version':1,'repository_group':OWNER,
        'groups':{OWNER:{'requires':[] if OWNER=='core' else ['core'],
            'core_components':[] if OWNER=='core' else ['coredep'],
            'verification_inputs':['main.c']}},'components':components,
        'targets':{'cpkt_'+OWNER+'_probe':{'group':OWNER,'kind':'executable',
            'public':False,'source_inputs':['main.c']}},
        'tests':{OWNER+'_behavior':{'group':OWNER,'execution':'runtime',
            'command_inputs':['main.c'],'requires':[]}}}
    for name,item in json.loads((ROOT/'cmake/components.json').read_text())['targets'].items():
        if item['kind']=='cmake-dashboard':data['targets'][name]=item
    (root/'cmake/components.json').write_text(json.dumps(data))
    presets={'version':3,'configurePresets':[]}
    for preset,kind in (('debug','Debug'),('release','Release')):
        presets['configurePresets'].append({'name':preset,'generator':generator,
            'binaryDir':'${sourceDir}/unused','cacheVariables':{'CMAKE_BUILD_TYPE':kind,
                'CPKT_TARGET_ARCH':'x86_64','CPKT_TARGET_OS':'linux',
                'CPKT_TARGET_LIBC':'gnu','CPKT_BUILD_TESTS':'ON'}})
    for preset in ('valgrind','fuzz','opcua-fuzz'):
        presets['configurePresets'].append({'name':preset,'inherits':'debug'})
    (root/'CMakePresets.json').write_text(json.dumps(presets))
    (root/'tests/phase.py').write_text('''from pathlib import Path
import sys
root=Path(sys.argv[1]);component=sys.argv[2];phase=sys.argv[3]
with (root/'events').open('a') as stream:stream.write(component+':'+phase+'\\n')
if phase=='install':
 output=root/'.cache/deps/x86_64-linux-gnu'/component/'install'
 output.mkdir(parents=True,exist_ok=True)
 (output/'static.a').write_text(component)
 (output/'shared.so').write_text(component)
''')
    recipe='''include(ExternalProject)
include("${CMAKE_SOURCE_DIR}/cmake/CpktVerifiedExternalProject.cmake")
function(cpkt_add_NAME)
 if(CPKT_BUILD_DEPENDENCIES)
  cpkt_external_project_add(NAME_project
   PREFIX "${CMAKE_SOURCE_DIR}/.cache/deps-build/${CPKT_TARGET_ID}/NAME"
   SOURCE_DIR "${CMAKE_SOURCE_DIR}/tests"
   DOWNLOAD_COMMAND "${CPKT_HOST_PYTHON_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/phase.py" "${CMAKE_SOURCE_DIR}" NAME extract
   CONFIGURE_COMMAND "${CPKT_HOST_PYTHON_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/phase.py" "${CMAKE_SOURCE_DIR}" NAME configure
   BUILD_COMMAND "${CPKT_HOST_PYTHON_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/phase.py" "${CMAKE_SOURCE_DIR}" NAME build
   INSTALL_COMMAND "${CPKT_HOST_PYTHON_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/phase.py" "${CMAKE_SOURCE_DIR}" NAME install)
  set_property(GLOBAL APPEND PROPERTY CPKT_DEPENDENCY_TARGETS NAME_project)
 endif()
endfunction()
cpkt_prepare_dependency_component(NAME NAME
 BUILD_ROOT "${CPKT_DEPENDENCY_BUILD_ROOT}/NAME"
 INSTALL_ROOT "${CPKT_EXTERNAL_ROOT}/NAME/install"
 INPUT_FILES "${CMAKE_SOURCE_DIR}/tests/phase.py" RECIPE_FUNCTIONS cpkt_add_NAME)
'''.replace('NAME',COMPONENT)
    recipe=recipe.replace('cpkt_prepare_dependency_component('+COMPONENT+' '+COMPONENT,
                          'cpkt_prepare_dependency_component(NAME '+COMPONENT)
    real=(ROOT/'cmake/CpktDependencies.cmake').read_text()
    loop=real[real.index('set(_all_dependency_targets "")'):real.rindex('endfunction()')]
    recipe+='function(cpkt_synthetic_producer)\n'+loop+'endfunction()\n'
    (root/'cmake/CpktDependencies.cmake').write_text(recipe)
    main='''cmake_minimum_required(VERSION 3.21)
include(cmake/CpktGroups.cmake)
include(cmake/CpktOperation.cmake)
project(synthetic LANGUAGES C)
set(_fixture_configuration "${CPKT_BUILD_TESTS};${CPKT_DEPENDENCY_BUILD_JOBS};${CPKT_ENABLE_FUZZING};${CPKT_FACADE_ONLY}")
include(CTest)
set(CPKT_TARGET_ID x86_64-linux-gnu CACHE STRING "")
set(CPKT_DEPENDENCY_BUILD_TYPE Release)
set(CPKT_DEPENDENCY_BUILD_ROOT "${CMAKE_SOURCE_DIR}/.cache/deps-build/${CPKT_TARGET_ID}")
set(CPKT_EXTERNAL_ROOT "${CMAKE_SOURCE_DIR}/.cache/deps/${CPKT_TARGET_ID}")
set(CPKT_DEPENDENCY_CONTRACT_ROOT "${CMAKE_SOURCE_DIR}/.cache/dependency-contracts")
set(CPKT_EXTERNAL_ROOT_LIFECYCLE_OWNED ON)
set(CPKT_DEPENDENCY_BUILD_ROOT_LIFECYCLE_OWNED ON)
include(cmake/CpktDependencyContract.cmake)
include(cmake/CpktDependencies.cmake)
if(CPKT_DEPENDENCY_PRODUCER)
 set(CPKT_ACTIVE_COMPONENTS COMPONENT)
 cpkt_synthetic_producer()
else()
 add_custom_target(cpkt_operation_guard COMMAND bash
  "${CMAKE_SOURCE_DIR}/scripts/operation.sh" --root "${CMAKE_SOURCE_DIR}" --group "${CPKT_GROUP}" --check VERBATIM)
 cpkt_group_add_executable(cpkt_${CPKT_GROUP}_probe main.c)
 cpkt_group_add_test(NAME ${CPKT_GROUP}_behavior COMMAND cpkt_${CPKT_GROUP}_probe)
 cpkt_group_set_tests_properties(${CPKT_GROUP}_behavior PROPERTIES LABELS example)
 cmake_language(DEFER CALL cpkt_validate_owned_graph)
endif()
'''.replace(' COMPONENT)', ' '+COMPONENT+')')
    (root/'CMakeLists.txt').write_text(main)
    return root,presets,main


def exercise(generator,parent):
    root,presets,main=setup(generator,parent)
    env={key:value for key,value in os.environ.items() if not key.startswith('CPKT_OPERATION_') and key not in ('GROUP','PRESET','CPKT_CONFIGURED_BINARY_DIR','CPKT_CONFIGURED_GROUP')}
    if OWNER!='core':
        failure=invoke(root,'build','--preset','debug',success=False,env=env)
        assert 'make deps-core' in failure.stderr
        assert not (root/'events').exists()
    external=prerequisite(root)
    external_bytes=external.read_bytes() if external else None
    def unchanged_core():
        if external:
            assert external.read_bytes()==external_bytes
            assert not (root/'build'/TARGET/'core').exists()
            assert not (root/'.cache/deps-build'/TARGET/'coredep').exists()
    def events():return (root/'events').read_text().splitlines()
    def rewrite_source(text):
        # Old Make implementations observe whole-second mtimes. Ensure each
        # deliberate source edit is newer than its previously compiled output.
        previous=int(time.time())
        while int(time.time())<=previous:time.sleep(0.02)
        (root/'main.c').write_text(text)
    invoke(root,'build','--preset','debug',env=env)
    assert events()==PHASES
    binary=root/'build'/TARGET/OWNER/'Debug'/('cpkt_'+OWNER+'_probe')
    receipt=root/'build/verification'/TARGET/OWNER/'Debug-development.json'
    for clean in (('--target','clean'),('--clean-first',)):
        direct=subprocess.run(['cmake','--build',str(binary.parent),*clean],cwd=root,env=env,capture_output=True,text=True)
        assert direct.returncode and 'operation delegation' in direct.stdout+direct.stderr,(direct.returncode,direct.stdout,direct.stderr)
        assert binary.is_file()
    invoke(root,'test','--preset','debug',env=env)
    identity=receipt.read_bytes();before=events()
    # A changed native launcher/stamp command is bookkeeping, not a compiled
    # component input. Both generators must retain exact installed bytes/runs.
    from cpkt_receipts import tree_identity
    installed=root/'.cache/deps'/TARGET/COMPONENT/'install'
    original_install=tree_identity(installed)
    component_receipt=root/'build/verification'/TARGET/OWNER/('component-'+COMPONENT+'.json')
    original_component=component_receipt.read_bytes()
    (root/'CMakeLists.txt').write_text(main+'\nset_property(GLOBAL PROPERTY RULE_LAUNCH_CUSTOM "${_cpkt_build_launcher} ")\n')
    invoke(root,'deps','--preset','debug',env=env)
    assert events()==before and tree_identity(installed)==original_install
    assert component_receipt.read_bytes()==original_component
    (root/'CMakeLists.txt').write_text(main)
    adapter=root/'cmake/CpktVerifiedExternalProject.cmake'
    adapter.write_text(adapter.read_text()+'\n# changed native recipe bytes\n')
    invoke(root,'test','--preset','debug',env=env)
    assert events()[len(before):]==PHASES
    assert tree_identity(installed)==original_install
    identity=receipt.read_bytes();before=events()

    invoke(root,'build','--preset','release',env=env)
    invoke(root,'build','--group','all','--preset','debug','--target',binary.name,env=env)
    assert events()==before and receipt.read_bytes()==identity
    unchanged_core()
    for foreign in set(('core','db','misc'))-{OWNER}:
        invoke(root,'build','--group',foreign,'--preset','debug',success=False,env=env)
    assert events()==before
    (root/'CMakeLists.txt').write_text(main+'\nmessage(FATAL_ERROR "intentional configure failure")\n')
    invoke(root,'configure','--preset','debug',success=False,env=env)
    assert not receipt.exists()
    (root/'CMakeLists.txt').write_text(main)
    invoke(root,'test','--preset','debug',env=env)
    artifact=root/'.cache/deps'/TARGET/COMPONENT/'install/static.a'
    value=artifact.read_bytes();stat=artifact.stat();artifact.write_bytes(b'corrupt')
    os.utime(artifact,ns=(stat.st_atime_ns,stat.st_mtime_ns))
    direct=subprocess.run(['bash',str(root/'scripts/operation.sh'),'--root',str(root),'--group',OWNER,'--',
        'cmake','--build',str(root/'build'/TARGET/OWNER/'producer'),'--target','cpkt_deps_'+COMPONENT],
        cwd=root,env=dict(env,CPKT_PRESET='release'),capture_output=True,text=True)
    assert direct.returncode and 'outputs missing/corrupt' in direct.stdout+direct.stderr
    assert 'Repair: make test GROUP='+OWNER+' PRESET=release' in direct.stdout+direct.stderr
    assert artifact.read_bytes()==b'corrupt' and events()==before

    invoke(root,'test','--preset','debug',env=env)
    assert artifact.read_bytes()==value and events()[len(before):]==PHASES
    unchanged_core()
    before=events()
    rewrite_source('int main(void) { return 1; }\n')
    invoke(root,'test','--preset','debug',success=False,env=env)
    assert not receipt.exists() and events()==before
    rewrite_source('int main(void) { return 0; }\n')
    for selection in (('--regex','behavior'),('--label','example')):
        invoke(root,'test','--group','all','--preset','debug',*selection,env=env)
        assert not receipt.exists()
    invoke(root,'test','--preset','debug',env=env)
    identity=receipt.read_bytes()
    invoke(root,'configure','--preset','valgrind',env=env)
    if OWNER!='db':invoke(root,'build','--preset','fuzz' if OWNER=='core' else 'opcua-fuzz',env=env)
    assert receipt.read_bytes()==identity and events()==before
    # Typed flag changes rebuild once. Warm retries and unrelated sources reuse.
    presets['configurePresets'][0]['cacheVariables']['CMAKE_C_FLAGS']={'type':'STRING','value':'-DREQUESTED_PRODUCER_FLAG=1'}
    (root/'CMakePresets.json').write_text(json.dumps(presets))
    invoke(root,'test','--preset','debug',env=env)
    assert events()[len(before):]==PHASES
    for selected in ('debug','release','debug'):
        prior=events();invoke(root,'test','--preset',selected,env=env)
        assert events()[len(prior):]==([] if selected=='debug' and prior==events() else PHASES)
        warm=events();invoke(root,'test','--preset',selected,env=env);assert events()==warm
    before=events();invoke(root,'build','--group','all','--preset','debug','--fresh',env=env)
    assert events()==before and not receipt.exists()
    invoke(root,'test','--preset','debug',env=env)
    sibling=root/'build'/TARGET/'foreign/sentinel';sibling.parent.mkdir(parents=True);sibling.write_text('untouched')
    inode=(root/'build/control/operation.lock').stat().st_ino
    subprocess.run(['bash',str(root/'scripts/clean.sh'),'clean','--group',OWNER],env=env,check=True,capture_output=True,text=True)
    assert sibling.read_text()=='untouched' and not artifact.exists()
    assert inode==(root/'build/control/operation.lock').stat().st_ino
    unchanged_core()
    subprocess.run(['bash',str(root/'scripts/clean.sh'),'clean','--group','all'],env=env,check=True,capture_output=True,text=True)
    assert inode==(root/'build/control/operation.lock').stat().st_ino
    assert sorted(path.name for path in (root/'build').iterdir())==['control']
    assert not (root/'scripts/__pycache__').exists()
    print(generator+': owning producer reuse, failure revocation, immutable prerequisite and cleanup passed')


if __name__=='__main__':
    (ROOT/'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='producer-reuse-',dir=ROOT/'build') as temporary:
        for generator in ('Ninja','Unix Makefiles'):exercise(generator,Path(temporary))
