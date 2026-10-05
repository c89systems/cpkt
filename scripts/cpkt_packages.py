#!/usr/bin/env python3
"""Inventory-driven split SDK staging, composition and scoped artifact proofs."""
import argparse
import ast
from collections import Counter
import fnmatch
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile

from cpkt_inventory import load, components_for, GROUPS, REPOSITORY_GROUP
from cpkt_operation import delegated, run as locked_run, operation_fds, child_delegation
from cpkt_presets import preset_info
from cpkt_receipts import cache, read, readiness_path, validate_component, verification_inputs, group_outputs, tree_identity, file_identity, tool_runtime_inputs

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('sdk_validator', ROOT/'scripts/validate-sdk.py')
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)
canonical, sha = validator.canonical, validator.sha


def command(args, cwd=ROOT, capture=False, group=None, env=None):
    scope = group or os.environ.get('CPKT_OPERATION_SCOPE', 'all')
    with child_delegation(ROOT, scope) as (delegation, fds):
        if env:
            delegation.update(env)
        from cpkt_package_command import run
        phase='configure' if str(args[0])=='cmake' and '--build' not in args else 'build' if '--build' in args else 'fixture' if any('preflight' in str(a) for a in args) else 'test' if any('consumer' in str(a) or 'group-build' in str(a) for a in args) else 'package'
        return run(args,root=ROOT,phase=phase,env=delegation,pass_fds=fds,capture=capture,cwd=cwd)


def safe_owned(path):
    path = Path(path)
    if '..' in path.parts:
        raise ValueError('owned mutation path contains parent traversal')
    path = path.absolute()
    if not path.is_relative_to(ROOT):
        raise ValueError('mutation outside repository')
    for parent in (path, *path.parents):
        if parent == ROOT:
            break
        if parent.is_symlink():
            raise ValueError('owned path has a symlink ancestor: ' + str(parent))
    return path


def write_json(path, value):
    safe_owned(path).parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_bytes(canonical(value))
    temporary.chmod(0o644)
    temporary.replace(path)


def version():
    return command(['bash', ROOT/'scripts/release-version.sh', ROOT], capture=True).strip()


def archive_name(ver, target, group):
    return f'cpkt-{ver}-{target}.tar.gz'


def prefix_name(ver, target):
    return f'cpkt-{ver}-{target}'


def stage_dir(target, group):
    return ROOT/'build/package-stage'/target/group


def proof_path(target, group):
    return ROOT/'build/verification'/target/group/'package-ready.json'


def selected_archive(ver, target, group):
    return stage_dir(target, group)/'archives'/archive_name(ver,target,group)


def invalidate_release(ver):
    # Before any dist payload replacement, old full proof loses publication power.
    for path in [ROOT/'dist'/f'cpkt-{ver}-CHECKSUMS', ROOT/'build/verification/release'/ver/'proof.json']:
        safe_owned(path).unlink(missing_ok=True)


def copy_file(source, destination):
    if destination.exists() or destination.is_symlink():
        raise ValueError('regular/symlink payload collision: ' + str(destination))
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_symlink():
        destination.symlink_to(os.readlink(source))
    elif source.is_file():
        shutil.copy2(source, destination)
    else:
        raise ValueError('missing product input: ' + str(source))


def safe_extract(archive, parent, root_name, destination_root=None):
    destination_root = destination_root or root_name
    validator.path_name(destination_root)
    if "/" in destination_root:raise ValueError("destination root must be one directory")
    parent = safe_owned(parent)
    parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive, 'r:gz') as stream:
        members = stream.getmembers()
        names = set()
        by_name = {m.name.rstrip('/'):m for m in members}
        for member in members:
            name = member.name.rstrip('/')
            validator.path_name(name)
            if name in names:
                raise ValueError('duplicate archive entry: ' + name)
            names.add(name)
            if name.split('/')[0] != root_name or not (member.isdir() or member.isfile() or member.issym()):
                raise ValueError('unexpected archive root/type: ' + name)
            if member.uid or member.gid:
                raise ValueError('archive owner is not 0/0: ' + name)
            if member.issym():
                if not member.linkname or PurePosixPath(member.linkname).is_absolute() or '\\' in member.linkname:
                    raise ValueError('unsafe archive symlink: ' + name)
                normalized = os.path.normpath(str(PurePosixPath(name).parent/member.linkname))
                if normalized.split('/')[0] != root_name:
                    raise ValueError('escaping archive symlink: ' + name)
        for member in members:
            safe_owned(parent/destination_root/PurePosixPath(member.name.rstrip("/")).relative_to(root_name))
        for member in members:
            name = member.name.rstrip('/')
            for ancestor in PurePosixPath(name).parents:
                if str(ancestor) in names and by_name[str(ancestor)].issym():
                    raise ValueError('archive symlink ancestor: ' + name)
            destination = parent/destination_root/PurePosixPath(name).relative_to(root_name)
            if member.isdir():
                if destination.is_symlink() or (destination.exists() and not destination.is_dir()):
                    raise ValueError('directory replacement collision: ' + name)
                destination.mkdir(parents=True, exist_ok=True)
            else:
                if destination.exists() or destination.is_symlink():
                    raise ValueError('archive payload collision: ' + name)
                destination.parent.mkdir(parents=True, exist_ok=True)
                if member.issym():
                    destination.symlink_to(member.linkname)
                else:
                    with stream.extractfile(member) as source, destination.open('wb') as output:
                        shutil.copyfileobj(source, output)
                    destination.chmod(member.mode)
    return parent/destination_root


def tar_stage(prefix, archive):
    archive.parent.mkdir(parents=True, exist_ok=True)
    temporary = archive.with_name(archive.name+'.tmp')
    with temporary.open('wb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', filename='', mtime=0, compresslevel=6) as compressed, tarfile.open(fileobj=compressed, mode='w') as output:
        for path in [prefix] + sorted(prefix.rglob('*')):
            relative = prefix.name if path == prefix else prefix.name+'/'+path.relative_to(prefix).as_posix()
            info = output.gettarinfo(str(path), arcname=relative)
            info.uid=info.gid=0; info.uname=info.gname=''; info.mtime=0
            if info.isfile():
                with path.open('rb') as source:
                    output.addfile(info,source)
            else:
                output.addfile(info)
    temporary.replace(archive)


def file_records(prefix):
    return [dict(path=name, type='symlink', target=os.readlink(path)) if path.is_symlink() else
            dict(path=name, type='file', mode=format(stat.S_IMODE(path.stat().st_mode),'04o'),sha256=sha(path))
            for name,path in sorted(validator.walk(prefix))]


def make_manifest(prefix, group, ver, target, components, core=None, floor=None):
    result = dict(schema_version=1, group=group, release_version=ver, target_id=target,
        libc=None if target.endswith('darwin') else target.split('-')[-1],
        macos_deployment_target=floor if target.endswith('darwin') else None,
        components=sorted(components,key=lambda x:x['name']), files=file_records(prefix),
        requires_core=None if group=='core' else {k:core[k] for k in ('package_id','release_version','target_id')})
    result['package_id']=hashlib.sha256(canonical(result)).hexdigest()
    destination=prefix/'share/cpkt/packages'/f'{group}.json'
    write_json(destination,result)
    return result


def package_inventory(data, group):
    names=components_for(data,group)
    cmake=[];pc=[];patterns=[]
    for name in names:
        item=data['components'][name]['package']
        cmake+=item['cmake'];pc+=item['pkgconfig'];patterns+=item['owned_patterns']
        for facade in item['facades']:
            cmake.append(facade['cmake']);pc.append(facade['pkgconfig'])
            stem=facade.get('output_name',facade['target'])
            patterns+=['lib/lib'+stem+'.*']
            patterns+=['include/cpkt/'+Path(h).name for h in facade['headers'] if not h.startswith('@')]
            if any(h.startswith('@') for h in facade['headers']):patterns+=['include/cpkt/'+facade['target'].removeprefix('cpkt_')+'*.h']
        for path in cmake:
            patterns+=['lib/cmake/'+path+'/*']
        for path in pc:
            patterns+=['lib/pkgconfig/'+path+'.pc']
    patterns+=['share/doc/cpkt/'+group+'/*']
    patterns+=[item['path'] for item in data['groups'][group]['package']['files']]
    return sorted(set(cmake)),sorted(set(pc)),sorted(set(patterns))


def metadata(prefix, directory, configured, data, group):
    cmake_names,pc_names,_=package_inventory(data,group)
    variables=dict(configured,_stage_root=str(prefix),CPKT_SOURCE_DIR=str(ROOT),CPKT_METADATA_CMAKE=';'.join(cmake_names),CPKT_METADATA_PC=';'.join(pc_names),CPKT_TARGET_ID=configured['CPKT_TARGET_ID'])
    script=directory/'package-metadata-input.cmake'
    # Parent diagnostic controls are not package inputs. Replaying their normal
    # values changes child diagnostics and triggers CMP0218 on CMake 4.4+.
    diagnostics={'CMAKE_WARN_DEPRECATED','CMAKE_ERROR_DEPRECATED'}
    script.write_text('cmake_minimum_required(VERSION 3.21)\n'+''.join('set('+k+' [==['+v+']==])\n' for k,v in variables.items() if not k.startswith('_CMAKE') and k not in diagnostics)+'include("'+str(ROOT/'cmake/package_metadata.cmake')+'")\n')
    command(['cmake','-P',script],group=group)
    # Shared cmocka and its facade have distinct installed discovery names.
    if group=='core':
        suffix='.dylib' if configured['CPKT_TARGET_ID'].endswith('darwin') else '.so'
        native=prefix/'lib/cmake/cmocka'
        native.mkdir(parents=True,exist_ok=True)
        (native/'cmocka-config.cmake').write_text('''get_filename_component(_cpkt_cmocka_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
foreach(v static shared)
  if(v STREQUAL "static")
    set(suffix ".a")
  else()
    set(suffix "'''+suffix+'''")
  endif()
  if(NOT TARGET cpkt::cmocka_${v})
    add_library(cpkt::cmocka_${v} ${v} IMPORTED)
    set_target_properties(cpkt::cmocka_${v} PROPERTIES IMPORTED_LOCATION "${_cpkt_cmocka_prefix}/lib/libcmocka${suffix}" INTERFACE_INCLUDE_DIRECTORIES "${_cpkt_cmocka_prefix}/include")
    if(v STREQUAL "static")
      set_property(TARGET cpkt::cmocka_static PROPERTY INTERFACE_COMPILE_DEFINITIONS CMOCKA_STATIC)
      set_property(TARGET cpkt::cmocka_static PROPERTY INTERFACE_LINK_LIBRARIES m)
    endif()
  endif()
endforeach()
if(NOT TARGET cmocka::cmocka)
  add_library(cmocka::cmocka ALIAS cpkt::cmocka_shared)
endif()
set(CMOCKA_LIBRARY cmocka::cmocka)
set(CMOCKA_LIBRARIES cmocka::cmocka)
'''.replace('${v} IMPORTED','${_cpkt_cmocka_type} IMPORTED').replace('  if(NOT TARGET cpkt::cmocka_${v})','  string(TOUPPER "${v}" _cpkt_cmocka_type)\n  if(NOT TARGET cpkt::cmocka_${v})'))
        native_version = configured['CPKT_CMOCKA_VERSION']
        version_template = 'set(PACKAGE_VERSION "{version}")\nif(PACKAGE_FIND_VERSION VERSION_GREATER PACKAGE_VERSION)\n set(PACKAGE_VERSION_COMPATIBLE FALSE)\nelse()\n set(PACKAGE_VERSION_COMPATIBLE TRUE)\n if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)\n  set(PACKAGE_VERSION_EXACT TRUE)\n endif()\nendif()\n'
        (native/'cmocka-config-version.cmake').write_text(version_template.format(version=native_version))
        facade=prefix/'lib/cmake/CpktCmocka'
        facade.mkdir(parents=True,exist_ok=True)
        (facade/'CpktCmockaConfig.cmake').write_text('''include(CMakeFindDependencyMacro)
find_dependency(cmocka CONFIG REQUIRED)
get_filename_component(_cpkt_cmocka_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
foreach(v static shared)
  if(v STREQUAL "static")
    set(suffix ".a")
  else()
    set(suffix "'''+suffix+'''")
  endif()
  string(TOUPPER "${v}" type)
  if(NOT TARGET cpkt::cmocka_facade_${v})
    add_library(cpkt::cmocka_facade_${v} ${type} IMPORTED)
    set_target_properties(cpkt::cmocka_facade_${v} PROPERTIES IMPORTED_LOCATION "${_cpkt_cmocka_prefix}/lib/libcpkt_cmocka${suffix}" INTERFACE_INCLUDE_DIRECTORIES "${_cpkt_cmocka_prefix}/include" INTERFACE_LINK_LIBRARIES "cpkt::cmocka_${v};m")
  endif()
endforeach()
''')
        (facade/'CpktCmockaConfigVersion.cmake').write_text(version_template.format(version=configured['CPKT_BUNDLE_VERSION']))
        for name,libs,cflags,requires in [('cmocka','-lcmocka','',''),('cmocka-shared','-lcmocka','',''),('cmocka-static','${libdir}/libcmocka.a','-DCMOCKA_STATIC',''),('cpkt-cmocka','-lcpkt_cmocka','','cmocka')]:
            (prefix/'lib/pkgconfig'/f'{name}.pc').write_text('prefix=${pcfiledir}/../..\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: '+name+'\nDescription: cmocka native/C89 test SDK\nVersion: '+(configured['CPKT_BUNDLE_VERSION'] if name=='cpkt-cmocka' else configured['CPKT_CMOCKA_VERSION'])+'\nLibs: -L${libdir} '+libs+'\nLibs.private: -lm\nCflags: -I${includedir} '+cflags+'\n'+('Requires: '+requires+'\n' if requires else ''))
        (prefix/'lib/pkgconfig/cpkt-core.pc').write_text('prefix=${pcfiledir}/../..\nName: cpkt-core\nDescription: Validated cpkt core identity marker\nVersion: '+configured['CPKT_BUNDLE_VERSION']+'\nLibs:\nCflags:\n')
    for config in (prefix/'lib/cmake').rglob('*.cmake'):
        text=config.read_text()
        text=re.sub(r'find_dependency\(([^\s)]+)([^)]*\bCONFIG\b[^)]*)\)',lambda m:'find_dependency('+m[1]+m[2]+' PATHS "${_cpkt_sdk_prefix}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)',text)
        if re.search(r'(?:Config|config)\.cmake$',config.name) and not re.search(r'(?:ConfigVersion|config-version)\.cmake$',config.name):
            text='include("${CMAKE_CURRENT_LIST_DIR}/../CpktSDK/Validate.cmake")\ncpkt_sdk_validate('+group+')\ncpkt_sdk_assert_targets('+group+')\n'+text+'\ncpkt_sdk_assert_targets('+group+')\n'
        config.write_text(text)
    for pc in (prefix/'lib/pkgconfig').glob('*.pc'):
        if pc.name=='cpkt-core.pc':continue
        text=pc.read_text()
        core_version=configured['CPKT_BUNDLE_VERSION'] if group=='core' else prepared_core(configured['CPKT_BUNDLE_VERSION'],configured['CPKT_TARGET_ID'],os.environ.get('CPKT_PRESET','debug'))[1]['release_version']
        marker='cpkt-core = '+core_version
        match=re.search(r'(?m)^Requires:(.*)$',text)
        text=text[:match.end()]+', '+marker+text[match.end():] if match else text+'Requires: '+marker+'\n'
        pc.write_text(text)


def abi_records(prefix, configured):
    records={}
    data=load(ROOT)
    modules=[pattern for item in data['components'].values() for pattern in item['package'].get('modules',[])]
    for library in sorted((prefix/'lib').rglob('*')):
        if library.is_symlink() or not library.is_file() or not re.search(r'\.so(?:\.|$)|\.dylib$',library.name):continue
        relative=library.relative_to(prefix).as_posix()
        module=any(fnmatch.fnmatchcase(relative,p) for p in modules)
        if configured['CPKT_TARGET_ID'].endswith('darwin'):
            tool=configured.get('CMAKE_OTOOL') or configured.get('CPKT_OTOOL')
            if not tool or not Path(tool).is_file():raise ValueError('external-tool-unavailable: Darwin otool')
            header=command([tool,'-hv',library],capture=True)
            names=command([tool,'-D',library],capture=True).splitlines()[1:]
            links=command([tool,'-L',library],capture=True).splitlines()[1:]
            if not names:
                if not module or not re.search(r'\bBUNDLE\b',header):raise ValueError('shared library has no install name: '+str(library))
                records[relative]={'kind':'module','install_name':None,'compatibility':None,'loader_name':library.name}
            else:
                if not links:raise ValueError('missing Darwin compatibility identity: '+str(library))
                records[relative]={'install_name':names[0].strip(),'compatibility':links[0].strip()}
        else:
            tool=configured.get('CMAKE_READELF')
            if not tool or not Path(tool).is_file():raise ValueError('target readelf unavailable')
            output=command([tool,'-d',library],capture=True)
            match=re.search(r'\(SONAME\).*?\[(.*?)\]',output)
            if not match:
                if not module:raise ValueError('shared library has no SONAME: '+str(library))
                records[relative]={'kind':'module','soname':None,'loader_name':library.name}
            else:records[relative]={'soname':match[1]}
    return records


def stage(preset,group,ver):
    data=load(ROOT);_,target,configuration=preset_info(ROOT,preset)
    if configuration!='Release':raise ValueError('package requires a release preset')
    directory=ROOT/'build'/target/group/configuration
    configured=cache(directory/'CMakeCache.txt')
    development=read(readiness_path(ROOT,target,group,configuration))
    if development['verification_id']!=verification_inputs(ROOT,group,configured) or development['outputs']!=group_outputs(directory):
        raise ValueError('development proof stale; run make test GROUP='+group+' PRESET='+preset)
    proof_path(target,group).unlink(missing_ok=True)
    core=None
    if group!='core':
        core_archive,core=prepared_core(ver,target,preset)
    workspace=safe_owned(stage_dir(target,group));workspace.mkdir(parents=True,exist_ok=True)
    prefix=workspace/prefix_name(ver,target)
    if prefix.exists():shutil.rmtree(prefix)
    prefix.mkdir()
    components=[]
    for name in components_for(data,group):
        item=data['components'][name];validate_component(ROOT,target,name)
        install=ROOT/'.cache/deps'/target/item['directory']/'install'
        for sub in ('include','lib'):
            for source in sorted((install/sub).rglob('*')):
                if source.is_dir() and not source.is_symlink():continue
                rel=source.relative_to(install).as_posix()
                if rel.endswith('.la') or rel.startswith(('lib/pkgconfig/','lib/cmake/','lib/engines-','lib/ossl-modules/')):continue
                copy_file(source,prefix/rel)
        license_path=item['package']['license']
        license_source=ROOT/license_path[1:] if license_path.startswith('@') else ROOT/'.cache/deps-build'/target/item['directory']/license_path
        copy_file(license_source,prefix/'share/doc/cpkt'/group/'third_party'/name/'LICENSE')
        for facade in item['package']['facades']:
            stem=facade.get('output_name',facade['target'])
            files=list(directory.glob('lib'+stem+'.*'))
            if not any(x.suffix=='.a' for x in files) or not any('.so' in x.name or '.dylib' in x.name for x in files):raise ValueError('missing both facade variants: '+stem)
            for source in sorted(files):copy_file(source,prefix/'lib'/source.name)
            for header in facade['headers']:
                sources=list((directory/'generated'/facade['generated']).rglob('*.h')) if header.startswith('@') else [ROOT/'include/cpkt'/header]
                for source in sources:
                    if 'include/cpkt/' in source.as_posix():rel=source.as_posix().split('include/',1)[1]
                    elif header.startswith('@'):
                        if source.parent.name!='cpkt':continue
                        rel='cpkt/'+source.name
                    else:rel='cpkt/'+source.name
                    if source.name.endswith('_bridge.inc') or 'private' in source.name:continue
                    if (prefix/'include'/rel).exists():
                        if sha(source)!=sha(prefix/'include'/rel):raise ValueError('header collision: '+rel)
                    else:copy_file(source,prefix/'include'/rel)
        producer=cache(ROOT/'build'/target/group/'producer/CMakeCache.txt')
        recipe=(ROOT/'cmake/CpktDependencies.cmake').read_text()
        from cpkt_receipts import function_text
        recipe=function_text(recipe,item['recipe_functions'][-1])
        hashes=re.findall(r'SHA256=([0-9a-f]{64})',recipe)
        if not hashes:raise ValueError('missing pinned source digest: '+name)
        version_key=item['package']['version_variable']
        if version_key not in producer:raise ValueError('missing component version: '+name)
        primary=hashes[0]
        if item['package'].get('primary_source_project'):
            project=item['package']['primary_source_project']
            block=re.search(r'cpkt_cached_external_project_add\(\$\{'+re.escape(project)+r'\}.*?URL_HASH\s+"SHA256=([0-9a-f]{64})"',recipe,re.S)
            if not block:raise ValueError('missing primary compiled source project: '+name)
            primary=block[1]
        components.append(dict(name=name,version=producer[version_key],source_sha256=primary,features={'variants':item['variants'],'recipe_sha256':hashlib.sha256(recipe.encode()).hexdigest(),'recipe_inputs':{p:sha(ROOT/p) for p in sorted(item['recipe_inputs'])},'source_digests':sorted(set(hashes)), 'options':{k:v for k,v in producer.items() if k.startswith('CPKT_'+name.upper().replace('-','_')+'_') and not k.endswith(('PREFIX','DIR','ROOT','LIBRARY'))}},abi={}))
    for record in components:
        for facade in data['components'][record['name']]['package']['facades']:
            for feature,variable in facade.get('features',{}).items():
                if variable not in configured:raise ValueError('missing compiled facade feature '+variable)
                record['features'][feature]=configured[variable]
    docs=prefix/'share/doc/cpkt'/group
    copy_file(ROOT/'LICENSE',docs/'LICENSE')
    (docs/'THIRD_PARTY_NOTICES.md').write_text('Bundled '+group+' components and complete license paths:\n\n'+''.join('- '+item['name']+' '+item['version']+': third_party/'+item['name']+'/LICENSE\n' for item in components))
    (docs/'README.md').write_text('cpkt '+ver+' '+group+' SDK\n\nValidate this installation before CMake/pkg-config discovery. '+('Independently usable core.' if group=='core' else 'Requires the exact core package identity recorded in packages/'+group+'.json.')+'\n')
    for source in data['groups'][group]['package']['docs']:copy_file(ROOT/source,docs/'docs'/Path(source).name)
    for source in data['groups'][group]['package']['examples']:copy_file(ROOT/source,docs/'examples'/Path(source).relative_to('examples'))
    for notice in data['groups'][group]['package']['extra_notices']:
        for source in sorted((ROOT/notice).rglob('*')):
            if source.is_file():copy_file(source,docs/'third_party'/Path(notice).name/source.relative_to(ROOT/notice))
    for item in data['groups'][group]['package']['files']:copy_file(ROOT/item['source'],prefix/item['path'])
    if group=='core':
        copy_file(ROOT/'LICENSE',prefix/'share/doc/cpkt/LICENSE')
        copy_file(ROOT/'docs/sdk-installation.md',prefix/'share/doc/cpkt/README.md')
        copy_file(ROOT/'scripts/validate-sdk.py',prefix/'share/cpkt/validate-sdk.py')
        write_json(prefix/'share/cpkt/payload-ownership.json',json.loads((ROOT/'cmake/payload-ownership.json').read_text()))
        copy_file(ROOT/'cmake/CpktSDKValidate.cmake',prefix/'lib/cmake/CpktSDK/Validate.cmake')
        # MQTT source support is part of core's installed consumer closure.
        support=ROOT/'.cache/deps'/target/'mqtt-c/install/share/cpkt/mqtt-c'
        if support.is_dir():
            for source in sorted(support.rglob('*')):
                if source.is_file():copy_file(source,prefix/'share/cpkt/mqtt-c'/source.relative_to(support))
    if group == 'misc' and not target.endswith('darwin'):
        for key in ('CPKT_CXX_STDLIB_STATIC_LIBRARY','CPKT_CXX_LIBGCC_STATIC_LIBRARY'):
            if configured.get(key):copy_file(Path(configured[key]),prefix/'lib/cpkt-cxx'/Path(configured[key]).name)
    metadata(prefix,workspace,configured,data,group)
    all_abi=abi_records(prefix,configured)
    for component in components:
        native=data['components'][component['name']]['package']['owned_patterns']
        facade_patterns=['lib/lib'+f.get('output_name',f['target'])+'.*' for f in data['components'][component['name']]['package']['facades']]
        component['abi']={k:v for k,v in all_abi.items() if any(fnmatch.fnmatchcase(k,p) for p in native+facade_patterns)} or {'shared_surface': 'header-only' if not facade_patterns else 'missing'}
        if component['abi']=={'shared_surface':'missing'}:raise ValueError('missing component ABI evidence')
    manifest=make_manifest(prefix,group,ver,target,components,core,configured.get('CPKT_MACOS_DEPLOYMENT_TARGET',configured.get('CMAKE_OSX_DEPLOYMENT_TARGET')))
    archive=selected_archive(ver,target,group)
    tar_stage(prefix,archive)
    return archive,manifest


def prepared_core(ver,target,preset):
    if REPOSITORY_GROUP == 'core':
        raise ValueError('core owns its SDK; it has no external core prerequisite')
    from cpkt_core import validate, cached_archive
    return cached_archive(ROOT,target), validate(ROOT,target)


def extract_core(archive, parent, ver, target):
    from cpkt_core import pin
    item=pin(ROOT,target)
    return safe_extract(archive,parent,'cpkt-'+item['version']+'-'+target,prefix_name(ver,target))


def artifacts(ver,scope,data=None):
    data=data or load(ROOT)
    result=[archive_name(ver,t,g) for t in data['package_targets'] for g in data['groups']]
    result.append(f'cpkt-{ver}-arm64-apple-darwin-smoke-test.zip')
    if scope=='release':result.append(f'cpkt-{ver}.tar.gz')
    return sorted(result)


def source_proof(ver, current_run=True):
    archive=ROOT/'dist'/f'cpkt-{ver}.tar.gz'
    proof=read(ROOT/'build/verification/source'/ver/'proof.json')
    fields={'schema_version','status','kind','archive_sha256','release_version','run','coverage','composition'}
    if set(proof)!=fields or type(proof.get('schema_version')) is not int or proof.get('schema_version')!=1 or proof.get('status')!='passed' or proof.get('release_version')!=ver or set(proof.get('coverage',{}))!={REPOSITORY_GROUP}:
        raise ValueError('complete successful source reconstruction evidence required')
    for group,evidence in proof['coverage'].items():
        if not evidence.get('outputs') or not evidence.get('tests') or not evidence.get('consumer_cases') or any(c.get('status')!='passed' or c.get('runtime')!='native' for c in evidence['consumer_cases']):
            raise ValueError('source reconstruction lacks actual compiled/test/consumer evidence for '+group)
    composition=proof['composition']
    orders=[['core']] if REPOSITORY_GROUP=='core' else [['core',REPOSITORY_GROUP],[REPOSITORY_GROUP,'core']]
    if composition.get('status')!='passed' or [c.get('order') for c in composition.get('combinations',[])]!=orders or any(not c.get('consumer_cases') for c in composition['combinations']):
        raise ValueError('source reconstruction lacks complete installed combinations')
    if proof.get('kind')!='source-reconstruction' or proof['archive_sha256']!=sha(archive) or (current_run and proof.get('run')!=os.environ['CPKT_OPERATION_RUN']):
        raise ValueError('current successful independent source reconstruction required')
    return proof


def checksum_snapshot(ver,scope,group=None,target=None,current_run=True):
    if scope=='selected':
        base=selected_archive(ver,target,group).parent
        names=[archive_name(ver,target,group)]
        destination=ROOT/'build/verification'/target/group/'CHECKSUMS'
    else:
        base=ROOT/'dist';names=artifacts(ver,scope)
        if scope=='release':source_proof(ver,current_run)
        destination=base/f'cpkt-{ver}-CHECKSUMS' if scope=='release' else ROOT/'build/verification/binary'/ver/'CHECKSUMS'
    destination.parent.mkdir(parents=True,exist_ok=True)
    content=''.join(sha(base/name)+'  '+name+'\n' for name in names)
    if scope!='selected':
        expected=set(names)|({f'cpkt-{ver}.tar.gz',f'cpkt-{ver}-CHECKSUMS'} if scope=='binary' else {destination.name})
        unexpected=[p.name for p in base.iterdir() if p.is_file() and p.name not in expected]
        if unexpected:raise ValueError('unexpected distribution payloads: '+','.join(unexpected))
    temporary=destination.with_suffix('.tmp');temporary.write_text(content);temporary.replace(destination)
    return destination,base


def check_snapshot(manifest,base,ver,scope,group=None,target=None, current_run=True):
    expected=set([archive_name(ver,target,group)] if scope=='selected' else artifacts(ver,scope))
    actual={}
    for line in manifest.read_text().splitlines():
        match=re.fullmatch(r'([0-9a-f]{64})  ([^/\\]+)',line)
        if not match or match[2] in actual:raise ValueError('invalid/duplicate checksum entry')
        actual[match[2]]=match[1]
    if set(actual)!=expected:raise ValueError('checksum inventory does not match '+scope+' scope')
    for name,digest in actual.items():
        if sha(base/name)!=digest:raise ValueError('checksum mismatch: '+name)
    if scope!='selected':
        allowed=expected|{f'cpkt-{ver}-CHECKSUMS'}
        if scope=='binary':
            allowed|={f'cpkt-{ver}.tar.gz',f'cpkt-{ver}-CHECKSUMS'}
        unexpected=sorted(path.name for path in base.iterdir() if path.name not in allowed)
        if unexpected:raise ValueError('unexpected distribution payloads: '+','.join(unexpected))
    if scope=='release':source_proof(ver,current_run)
    return actual


def privacy(paths):
    command(['cmake','-DCPKT_ROOT='+str(ROOT),'-DCPKT_SCAN_LABEL=group artifacts','-DCPKT_SCAN_PATHS='+';'.join(map(str,paths)),'-P',ROOT/'tests/privacy_scan.cmake'])


def consumer_context(root,target,preset,group,archives,configured):
    data=load(root)
    inputs=set()
    for name,item in data['installed_consumers'].items():
        if item['group'] in (group,'all'):
            inputs.update([item['source']]+item.get('extra_sources',[]))
    # Helpers determine compiler flags, import guards, inspection and runtime.
    inputs.update(str(p.relative_to(root)) for p in (root/'scripts').glob('cpkt_*.py'))
    inputs.update(['scripts/validate-sdk.py','scripts/run-no-warnings.sh','scripts/cpkt-toolchains.sh'])
    inputs.update(str(p.relative_to(root)) for p in (root/'cmake').glob('*.cmake'))
    for component in data['components'].values():
        if component['group']==group:
            inputs.update(f['export_catalog'] for f in component['package']['facades'] if 'export_catalog' in f)
    for name,item in data['installed_examples'].items():
        if item['group']==group:inputs.update(str(p.relative_to(root)) for p in (root/'examples'/Path(name).name).rglob('*') if p.is_file())
    inputs.update(name for name in ['tests/auth_package_discovery_test.py','tests/darwin_curl_package_test.sh'] if (root/name).is_file())
    pending=list(inputs)
    while pending:
        name=pending.pop();path=root/name
        if path.suffix not in ('.py','.sh','.cmake'):continue
        text=path.read_text()
        children=set(re.findall(r'(?:scripts|tools|tests|cmake|skills)/[A-Za-z0-9_./-]+\.(?:py|sh|cmake|hpp|h|cpp|cxx|cc|c|json|patch|series|txt)(?![A-Za-z0-9_.])',text))
        if path.suffix=='.py':
            for node in ast.walk(ast.parse(text)):
                modules=([node.module] if isinstance(node,ast.ImportFrom) and node.module else [i.name for i in node.names] if isinstance(node,ast.Import) else [])
                for module in modules:
                    for directory in (path.parent,root/'scripts',root/'tools'):
                        child=directory/(module.replace('.','/')+'.py')
                        if child.is_file():children.add(child.relative_to(root).as_posix())
        for child in children:
            if child not in inputs and (root/child).is_file():inputs.add(child);pending.append(child)
    inputs={p for p in inputs if (root/p).is_file()}
    settings={k:str(v) for k,v in configured.items() if k.endswith('_ABI_VERSION') or k in ('CMAKE_C_COMPILER','CMAKE_CXX_COMPILER','CMAKE_NM','CMAKE_AR','CMAKE_READELF','CMAKE_OTOOL','CMAKE_SYSROOT','CMAKE_OSX_SYSROOT','CPKT_OSXCROSS_ROOT','CPKT_OSXCROSS_HOST','CPKT_DEPENDENCY_BUILD_JOBS','CMAKE_C_FLAGS','CMAKE_CXX_FLAGS','CMAKE_EXE_LINKER_FLAGS','CMAKE_SHARED_LINKER_FLAGS','CMAKE_OSX_DEPLOYMENT_TARGET','CPKT_MACOS_DEPLOYMENT_TARGET','CPKT_CXX_STDLIB_STATIC_LIBRARY','CPKT_CXX_LIBGCC_STATIC_LIBRARY')}
    tools={k:file_identity(Path(v).resolve()) for k,v in settings.items() if k in ('CMAKE_C_COMPILER','CMAKE_CXX_COMPILER','CMAKE_NM','CMAKE_AR','CMAKE_READELF','CMAKE_OTOOL','CPKT_CXX_STDLIB_STATIC_LIBRARY','CPKT_CXX_LIBGCC_STATIC_LIBRARY') and v}
    for name in ('cmake','pkg-config','make','ninja','codesign','xcrun'):
        path=shutil.which(name)
        if path:tools[name]=file_identity(Path(path).resolve())
    if target.startswith(('aarch64','armhf')) and not target.endswith('darwin'):
        runner=os.environ.get('CPKT_QEMU_AARCH64' if target.startswith('aarch64') else 'CPKT_QEMU_ARM','/usr/bin/qemu-aarch64' if target.startswith('aarch64') else '/usr/bin/qemu-arm')
        tools['runner']=file_identity(Path(runner).resolve())
    environment={k:v for k,v in os.environ.items() if k in ('CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','SDKROOT','LANG','LC_ALL','PATH','LD_LIBRARY_PATH','DYLD_LIBRARY_PATH','CC','CXX','CFLAGS','CXXFLAGS','LDFLAGS','CPKT_QEMU_ARM','CPKT_QEMU_AARCH64')}
    if target.endswith('darwin'):
        from cpkt_receipts import darwin_backend_inputs
        tools['compiler_backends']=darwin_backend_inputs(settings)
    return {'schema_version':1,'host':{'platform':sys.platform,'uname':list(os.uname())},'target':target,'preset':preset,'group':group,'archives':archives,'settings':settings,'environment':environment,
            'inputs':{p:file_identity(root/p) for p in sorted(inputs)},'tools':tools,
            'runtime':tool_runtime_inputs(settings.get('CMAKE_SYSROOT',''),settings.get('CMAKE_OSX_SYSROOT','')),
            'cases':{k:v for k,v in data['installed_consumers'].items() if v['group']==group},
            'examples':{k:v for k,v in data['installed_examples'].items() if v['group']==group}}


def consumer_evidence_path(target,group):return ROOT/'build/verification'/target/group/'consumer-evidence.json'


def publish_consumer_evidence(preset,target,group,archives,log,cases):
    from cpkt_sdk_consumer import configuration
    context=consumer_context(ROOT,target,preset,group,archives,configuration(target,preset))
    outputs=ROOT/'build/verification'/target/group/'installed-consumers'
    evidence={'schema_version':1,'status':'passed','run':os.environ['CPKT_OPERATION_RUN'],'context':context,'context_id':sha_bytes(canonical(context)),
              'consumer_cases':cases,'consumer_log':str(log.relative_to(ROOT)),'consumer_log_sha256':sha(log),
              'output_root':str(outputs.relative_to(ROOT)),'outputs':tree_identity(outputs)}
    validate_consumer_evidence(ROOT,evidence,context,os.environ['CPKT_OPERATION_RUN'])
    write_json(consumer_evidence_path(target,group),evidence)
    return evidence


def sha_bytes(value):return hashlib.sha256(value).hexdigest()


def expected_owned_cases(context):
    expected=Counter()
    for name,item in context['cases'].items():
        if item['kind']=='pic':continue
        expected[(name,False)]+=2
        if item.get('intentional_failure'):expected[(name,True)]+=2
        if item.get('pc') and not item.get('runtime_args'):
            for variant in ('static','shared'):expected[(name+'-pc-'+variant,False)]+=2
    if context['group']=='core' and context['target'].endswith('gnu') and any(item.get('pc')=='cpkt-sasl' for item in context['cases'].values()):expected[('sasl-fully-static',False)]+=2
    for item in context['examples'].values():
        expected[(item['target'],False)]+=2
        if item['pkg_config_script']:expected[(item['target']+'-pkg',False)]+=2
    return expected


def validate_consumer_evidence(root,evidence,context,run):
    if evidence.get('schema_version')!=1 or evidence.get('status')!='passed' or evidence.get('run')!=run or evidence.get('context')!=context or evidence.get('context_id')!=sha_bytes(canonical(context)):raise ValueError('owned consumer context is stale/unknown')
    cases=evidence.get('consumer_cases')
    if not cases or any(c.get('status') not in ('passed','deferred-native-runtime') for c in cases) or any(c.get('status')!='passed' for c in cases if not context['target'].endswith('darwin')):raise ValueError('owned consumer cases are incomplete/failed')
    if Counter((c.get('executable'),c.get('intentional_failure',False)) for c in cases)!=expected_owned_cases(context):raise ValueError('owned consumer exact required case coverage changed')
    if any(c.get('target')!=context['target'] for c in cases):raise ValueError('owned consumer target changed')
    for field in ('consumer_log','output_root'):
        validator.path_name(evidence[field])
        if not evidence[field].startswith('build/'):raise ValueError('consumer evidence outside build')
    log=root/evidence['consumer_log']
    if sha(log)!=evidence['consumer_log_sha256'] or json.loads(log.read_text().splitlines()[-1])!=cases or tree_identity(root/evidence['output_root'])!=evidence['outputs']:raise ValueError('owned consumer actual log/output identities changed')
    for case in cases:
        matches=[value['sha256'] for path,value in evidence['outputs'].items() if Path(path).name==case['executable'] and value['type']=='file']
        if case.get('executable_sha256') not in matches:raise ValueError('owned consumer executable identity changed')
    return evidence


def owned_archive_suite(preset,ver,target,base,group,allow_reuse=True):
    from cpkt_sdk_consumer import configuration
    order=['core',group] if group!='core' else ['core']
    archives={group:sha(base/archive_name(ver,target,group))}
    if group!='core':archives['core']=sha(prepared_core(ver,target,preset)[0])
    context=consumer_context(ROOT,target,preset,group,archives,configuration(target,preset))
    try:
        if not allow_reuse:raise ValueError('artifact lane requires its own archive suites')
        evidence=validate_consumer_evidence(ROOT,read(consumer_evidence_path(target,group)),context,os.environ['CPKT_OPERATION_RUN'])
        return evidence,'reused'
    except (OSError,ValueError,KeyError,RuntimeError,TypeError):pass
    safe_owned(consumer_evidence_path(target,group)).unlink(missing_ok=True)
    workspace=ROOT/'build/verification'/target/group/'archive-suite'
    safe_owned(workspace)
    if workspace.exists():shutil.rmtree(workspace)
    if group!='core':extract_core(prepared_core(ver,target,preset)[0],workspace,ver,target)
    prefix=safe_extract(base/archive_name(ver,target,group),workspace,prefix_name(ver,target))
    validator.validate(prefix,order,ver,target)
    output=command([sys.executable,ROOT/'scripts/cpkt_sdk_consumer.py','--prefix',prefix,'--target',target,'--groups',','.join(order),'--owners',group,'--preset',preset],capture=True,group=group)
    log=workspace/'consumer.log';log.write_text(output);cases=json.loads(output.splitlines()[-1])
    if not cases:raise ValueError('empty owned archive suite')
    evidence=publish_consumer_evidence(preset,target,group,archives,log,cases)
    validate_consumer_evidence(ROOT,evidence,context,os.environ['CPKT_OPERATION_RUN'])
    return evidence,'executed'


def verify_selected(preset,group,ver,archive=None):
    _,target,_=preset_info(ROOT,preset)
    archive=archive or selected_archive(ver,target,group)
    proof_path(target,group).unlink(missing_ok=True)
    workspace=stage_dir(target,group)/'verify'
    safe_owned(workspace)
    if workspace.exists():shutil.rmtree(workspace)
    workspace.mkdir(parents=True)
    order=['core',group] if group!='core' else ['core']
    if group!='core':
        core_archive,_=prepared_core(ver,target,preset)
        extract_core(core_archive,workspace,ver,target)
    prefix=safe_extract(archive,workspace,prefix_name(ver,target))
    validator.validate(prefix,order,ver,target)
    # Actual extracted libraries, exports, loader and all owned consumers.
    output=command([sys.executable,ROOT/'scripts/cpkt_sdk_consumer.py','--prefix',prefix,'--target',target,'--groups',','.join(order),'--owners',group,'--preset',preset],group=group,capture=True)
    (workspace/'consumer.log').write_text(output)
    cases=json.loads(output.splitlines()[-1])
    if not cases or any(c['status'] not in ('passed','deferred-native-runtime') for c in cases):raise ValueError('missing/failed actual installed consumer cases')
    if not target.endswith('darwin') and any(c['status']!='passed' for c in cases):raise ValueError('Linux runtime cases may not be deferred')
    privacy([archive])
    manifest=validator.load_manifest(prefix,group)
    closure={g:sha(archive if g==group else core_archive) for g in order}
    publish_consumer_evidence(preset,target,group,closure,workspace/'consumer.log',cases)
    write_json(proof_path(target,group),dict(schema_version=1,status='passed',kind='package-ready',group=group,target_id=target,release_version=ver,archive_sha256=sha(archive),manifest=manifest,coverage=['manifest','exports','loader','static-consumers','shared-consumers','relocation','privacy'],consumer_cases=cases,consumer_log_sha256=sha(workspace/'consumer.log'),development_id=verification_inputs(ROOT,group,cache(ROOT/'build'/target/group/'Release/CMakeCache.txt')),graph_outputs=group_outputs(ROOT/'build'/target/group/'Release'),run=os.environ['CPKT_OPERATION_RUN']))


def combinations(preset,ver,base,reuse_owned=True):
    _,target,_=preset_info(ROOT,preset)
    group=REPOSITORY_GROUP
    workspace=ROOT/'build/verification'/target/'all/packages'
    safe_owned(workspace)
    if workspace.exists():shutil.rmtree(workspace)
    evidence,accounting=owned_archive_suite(preset,ver,target,base,group,reuse_owned)
    orders=[['core']] if group=='core' else [['core',group],[group,'core']]
    results=[]
    for index,order in enumerate(orders):
        parent=workspace/str(index)
        for owner in order:
            if owner=='core' and group!='core':
                prefix=extract_core(prepared_core(ver,target,preset)[0],parent,ver,target)
            else:
                prefix=safe_extract(base/archive_name(ver,target,group),parent,prefix_name(ver,target))
        validator.validate(prefix,order,ver,target)
        output=command([sys.executable,ROOT/'scripts/cpkt_sdk_consumer.py','--prefix',prefix,
            '--target',target,'--groups',','.join(order),'--owners',group,'--preset',preset,
            '--composition'],capture=True)
        log=parent/'consumer.log';log.write_text(output)
        cases=json.loads(output.splitlines()[-1])
        if not cases or any(c['status'] not in ('passed','deferred-native-runtime') for c in cases):
            raise ValueError('composition lacks successful executed consumer cases')
        if not target.endswith('darwin') and any(c['status']!='passed' for c in cases):
            raise ValueError('Linux composition requires actual runtime cases')
        results.append({'order':order,'files':file_records(prefix),'consumer_cases':cases,
            'consumer_log_sha256':sha(log),'reused_owned_suites':{group:evidence['context_id']}})
    if len(results)>1 and results[0]['files']!=results[1]['files']:
        raise ValueError('independent package extraction orders differ')
    archives={group:sha(base/archive_name(ver,target,group))}
    if group!='core':archives['core']=sha(prepared_core(ver,target,preset)[0])
    write_json(workspace/'proof.json',dict(schema_version=1,status='passed',kind='installed-composition',
        target_id=target,release_version=ver,run=os.environ['CPKT_OPERATION_RUN'],archives=archives,
        owned_suites={group:evidence},owned_suite_accounting={group:accounting},combinations=results))
    return results


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['package','stage','verify','checksums','inventory','compose','stage-target'])
    parser.add_argument('--group',choices=GROUPS,default=os.environ.get('GROUP','all'))
    parser.add_argument('--preset',default=os.environ.get('PRESET'))
    parser.add_argument('--scope',choices=['selected','binary','release'],default=os.environ.get('SCOPE'))
    parser.add_argument('--version')
    parser.add_argument('--base',type=Path)
    args=parser.parse_args()
    scope=args.scope or ('binary' if args.group=='all' else 'selected')
    if (scope=='selected')!=(args.group!='all'):parser.error('selected scope requires a selected group; binary/release requires GROUP=all')
    if args.group!='all' and not args.preset:parser.error('selected packaging requires explicit release PRESET')
    if args.preset:
        _,target,configuration=preset_info(ROOT,args.preset)
        if configuration!='Release':parser.error('packaging requires a release preset')
    if args.action=='package':os.environ['CPKT_PACKAGE_ACTIVE']='1'
    if args.action=='inventory':
        print('\n'.join(artifacts(args.version or '0.0.0',scope)));return
    if 'CPKT_OPERATION_FD' not in os.environ:
        return locked_run(ROOT,args.group,[sys.executable,__file__]+sys.argv[1:])
    delegated(ROOT,args.group)
    if args.preset:os.environ['CPKT_PRESET']=args.preset
    ver=args.version or version()
    current_run=os.environ.get('CPKT_RELEASE_PRODUCTION')=='1'
    if args.group=='all' and args.action in ('package','checksums','verify'):
        # A failed rerun must not leave an earlier aggregate proof publishable.
        (ROOT/'build/verification'/scope/ver/'proof.json').unlink(missing_ok=True)
    if args.action=='checksums':
        snapshot,base=checksum_snapshot(ver,scope,args.group,target if args.preset else None,current_run=current_run)
        check_snapshot(snapshot,base,ver,scope,args.group,target if args.preset else None,current_run=current_run);return
    if args.action=='compose':
        if not args.preset or args.group!='all':parser.error('compose requires all and one release PRESET')
        combinations(args.preset,ver,args.base or ROOT/'dist');return
    if args.group!='all':
        if args.action=='verify':
            snapshot=ROOT/'build/verification'/target/args.group/'CHECKSUMS'
            check_snapshot(snapshot,selected_archive(ver,target,args.group).parent,ver,scope,args.group,target)
        if args.action=='package':
            command([sys.executable,ROOT/'group-build.py' if False else ROOT/'scripts/group-build.py','test','--group',args.group,'--preset',args.preset],group=args.group)
        if args.action in ('package','stage'):
            stage(args.preset,args.group,ver)
        verify_selected(args.preset,args.group,ver)
        if args.action!='verify':snapshot,base=checksum_snapshot(ver,scope,args.group,target)
        else:base=selected_archive(ver,target,args.group).parent
        check_snapshot(snapshot,base,ver,scope,args.group,target)
        return
    if args.action == 'stage-target':
        if not args.preset:parser.error('stage-target requires a release preset')
        for group in load(ROOT)['groups']:
            stage(args.preset,group,ver);verify_selected(args.preset,group,ver)
        return
    if args.preset:parser.error('all-matrix package/verify cannot narrow PRESET; use compose for a one-target proof')
    data=load(ROOT)
    if args.action=='package':
        # Structural/runtime preflight precedes every expensive producer.
        for target in data['package_targets']:
            command([sys.executable,ROOT/'scripts/group-build.py','preflight','--group','all','--preset',target+'-release'])
        invalidate_release(ver)
        (ROOT/'dist').mkdir(exist_ok=True)
        for target in data['package_targets']:
            preset=target+'-release'
            for group in data['groups']:
                command([sys.executable,ROOT/'scripts/group-build.py','test','--group',group,'--preset',preset],group=group)
                archive,_=stage(preset,group,ver)
                verify_selected(preset,group,ver)
                shutil.copy2(archive,ROOT/'dist'/archive.name)
            combinations(preset,ver,ROOT/'dist')
        command([sys.executable,ROOT/'scripts/cpkt_darwin.py','smoke-zip','--version',ver])
    else:
        base=ROOT/'dist'
        snapshot=base/f'cpkt-{ver}-CHECKSUMS' if scope=='release' else ROOT/'build/verification/binary'/ver/'CHECKSUMS'
        check_snapshot(snapshot,base,ver,scope,current_run=current_run)
        for target in data['package_targets']:combinations(target+'-release',ver,base)
        privacy([snapshot]+[base/name for name in artifacts(ver,scope)])
        write_json(ROOT/'build/verification'/scope/ver/'proof.json',dict(schema_version=1,status='passed',kind='artifact-'+scope,scope=scope,run=os.environ['CPKT_OPERATION_RUN'],manifest_sha256=sha(snapshot),artifacts=check_snapshot(snapshot,base,ver,scope,current_run=current_run)))


if __name__=='__main__':
    try:sys.exit(main())
    except (ValueError,RuntimeError,OSError,KeyError,TypeError) as error:sys.exit('package split: '+str(error))
