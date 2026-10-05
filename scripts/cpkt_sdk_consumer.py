#!/usr/bin/env python3
"""Run all inventory-owned installed consumers, exports and loader checks."""
import argparse
import copy
import fnmatch
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time

from cpkt_inventory import load, REPOSITORY_GROUP
from cpkt_packages import ROOT, command, validator, safe_owned, file_records, abi_records
from cpkt_receipts import cache
from cpkt_presets import preset_info
from cpkt_operation import delegated, run as locked_run
from cpkt_cmake_inputs import commands


def facade_abi_defaults(root):
    """Read release ABI declarations without configuring a producer graph."""
    required = {facade['abi_version_variable']
        for component in load(root)['components'].values()
        if not component.get('external')
        for facade in component['package']['facades']
        if facade.get('abi_version_variable')}
    values = {}
    for name, arguments, _ in commands((root/'CMakeLists.txt').read_text()):
        if name.lower() != 'set':
            continue
        first = re.match(r'\s*([A-Za-z_][A-Za-z0-9_]*)', arguments)
        if not first or first[1] not in required:
            continue
        tokens = shlex.split(arguments, comments=True)
        if not tokens or tokens[0] not in required:
            continue
        if len(tokens) < 2 or not re.fullmatch(r'0|[1-9][0-9]*', tokens[1]):
            raise ValueError('facade ABI declaration must be a literal integer: '+tokens[0])
        values[tokens[0]] = tokens[1]
    if required != set(values):
        raise ValueError('missing source facade ABI declarations: '+','.join(sorted(required-set(values))))
    return values


def inspect_notices(prefix,owners):
    for group in owners:
        docs=prefix/'share/doc/cpkt'/group
        for name in ('LICENSE','THIRD_PARTY_NOTICES.md','README.md'):
            path=docs/name
            if not path.is_file() or path.is_symlink() or not path.stat().st_size:raise ValueError('missing owned SDK notice: '+group+'/'+name)
        notices=(docs/'THIRD_PARTY_NOTICES.md').read_text()
        for component in validator.load_manifest(prefix,group)['components']:
            license_path=docs/'third_party'/component['name']/'LICENSE'
            if not license_path.is_file() or license_path.is_symlink() or not license_path.stat().st_size or component['name']+' '+component['version'] not in notices:raise ValueError('missing component notice/license: '+component['name'])


def inspect(prefix,target,configured,data,owners,workspace):
    inspect_notices(prefix,owners)
    expected_abi={}
    for group in owners:
        manifest=validator.load_manifest(prefix,group)
        for item in manifest['components']:
            expected_abi.update({k:v for k,v in item['abi'].items() if k.startswith('lib/')})
    actual=abi_records(prefix,configured)
    for name,value in expected_abi.items():
        if actual.get(name)!=value:raise ValueError('installed ABI identity changed: '+name)
    calls=['cmake_minimum_required(VERSION 3.21)', 'set(CPKT_PACKAGE_INSPECTION_ONLY TRUE)',
           'set(CPKT_TARGET_ID "'+target+'")', 'set(CPKT_NM "'+configured['CMAKE_NM']+'")',
           'set(CPKT_AR "'+configured['CMAKE_AR']+'")', 'set(CPKT_READELF "'+configured.get('CMAKE_READELF','')+'")',
           'set(CPKT_OTOOL "'+configured.get('CMAKE_OTOOL',configured.get('CPKT_OTOOL',''))+'")',
           'set(CPKT_ASSERTION_WORK_ROOT "'+str(workspace)+'")',
           'include("'+str(ROOT/'cmake/package_inspection.cmake')+'")']
    allowed={Path(k).name for k in actual}
    for name in actual:
        path=prefix/name
        if target.endswith('darwin'):
            calls+=['cpkt_assert_darwin_dylib_relocatable("'+str(path)+'" "'+name+'")',
                    'cpkt_assert_darwin_deployment_target("'+str(path)+'" "'+validator.load_manifest(prefix,'core')['macos_deployment_target']+'" "'+name+'")']
            if sys.platform == 'darwin':command(['codesign','--verify','--strict',path])
            output=command([configured['CMAKE_OTOOL'],'-L',path],capture=True)
            own_id=actual[name].get('install_name')
            dependencies=[line.strip().split(' ')[0] for line in output.splitlines()[1:] if line.strip().split(' ')[0]!=own_id]
            for dependency in dependencies:
                if dependency.startswith(('/usr/lib/','/System/Library/')):continue
                if not dependency.startswith('@rpath/') or not (prefix/'lib'/Path(dependency).name).exists():raise ValueError('Darwin dependency outside selected closure: '+dependency)
        else:
            calls+=['cpkt_assert_elf_runtime_metadata_file_relocatable("'+str(path)+'" "'+name+'")',
                    'cpkt_assert_elf_lacks_needed("'+str(path)+'" "(asan|ubsan|tsan|lsan|afl)" "'+name+'")']
            output=command([configured['CMAKE_READELF'],'-d',path],capture=True)
            for needed in re.findall(r'\(NEEDED\).*?\[(.*?)\]',output):
                if (prefix/'lib'/needed).exists():continue
                if not re.fullmatch(r'(?:libc|libm|libdl|librt|libpthread|libresolv|libutil|libgcc_s|libstdc\+\+|libatomic|ld-linux[^/]*|libc\.musl[^/]*)(?:\.so(?:\.[0-9]+)*)?',needed):raise ValueError('undeclared loader dependency: '+needed+' in '+name)
            for component in data['components'].values():
                if component['group'] not in owners:continue
                for pattern,required in component['package'].get('required_runpaths',{}).items():
                    if fnmatch.fnmatchcase(name,pattern):
                        calls+=['cpkt_assert_elf_runpath("'+str(path)+'" "'+required.replace('$','\\\\$')+'" "'+name+' required sibling lookup")']
    if 'db' in owners:
        postgres=next(item for item in validator.load_manifest(prefix,'db')['components'] if item['name']=='postgresql')
        major=postgres['version'].split('.')[0]
        module='libpq-oauth-'+major+('.dylib' if target.endswith('darwin') else '.so')
        if not (prefix/'lib'/module).is_file():raise ValueError('missing PostgreSQL private OAuth module: '+module)
        native=[prefix/name for name in actual if re.fullmatch(r'lib/libpq(?:\.so\.[0-9.]+|\.[0-9]+\.dylib)',name)]
        literal=('@loader_path/' if target.endswith('darwin') else '')+module
        if not native or not any(literal.encode() in p.read_bytes() for p in native):raise ValueError('PostgreSQL private OAuth loader identity missing: '+literal)
    for component in data['components'].values():
        if component['group'] not in owners:continue
        for facade in component['package']['facades']:
            for header,patterns in facade.get('header_forbidden',{}).items():
                text=(prefix/'include/cpkt'/header).read_text()
                for pattern in patterns:
                    if re.search(pattern,text):raise ValueError('facade header leaks forbidden token '+header+': '+pattern)
            stem=facade.get('output_name',facade['target'])
            shared=prefix/'lib'/('lib'+stem+('.dylib' if target.endswith('darwin') else '.so'))
            abi=str(configured[facade['abi_version_variable']]) if facade.get('abi_version_variable') else str(facade['abi_version'])
            if target.endswith('darwin'):
                calls+=['cpkt_assert_darwin_install_name("'+str(shared)+'" "@rpath/lib'+stem+'.'+abi+'.dylib" "'+stem+' facade ABI")']
            else:
                calls+=['cpkt_assert_elf_soname("'+str(shared)+'" "lib'+stem+'.so.'+abi+'" "'+stem+' facade ABI")']
                for forbidden in facade.get('forbidden_needed',[]):
                    calls+=['cpkt_assert_elf_lacks_needed("'+str(shared)+'" "'+forbidden+'" "'+stem+' closed backend")']
            if 'export_catalog' in facade:
                catalog=ROOT/facade['export_catalog']
                if not catalog.is_file():raise ValueError('missing authoritative export catalog: '+str(catalog))
                calls+=['cpkt_assert_dynamic_exports_equal("'+str(shared)+'" "'+str(catalog)+'" "'+stem+'")']
            else:
                calls+=['cpkt_assert_dynamic_exports_match("'+str(shared)+'" "'+facade['export_policy']+'" "'+stem+'")']
            if 'cmocka' not in stem and not target.endswith('darwin'):
                calls+=['cpkt_assert_elf_lacks_needed("'+str(shared)+'" "cmocka" "production '+stem+'")']
            calls+=['cpkt_assert_static_archive_lacks_lto("'+str(prefix/'lib'/('lib'+stem+'.a'))+'" "'+stem+'")']
    script=workspace/'inspection.cmake';script.write_text('\n'.join(calls)+'\n');command(['cmake','-P',script])


def configuration(target,preset):
    _,configured_target,mode=preset_info(ROOT,preset)
    if target!=configured_target:raise ValueError('consumer preset target mismatch')
    path=ROOT/'build'/target/REPOSITORY_GROUP/mode/'CMakeCache.txt'
    prior=cache(path) if path.is_file() else {}
    if sys.platform=='darwin' and target=='arm64-apple-darwin':
        from cpkt_darwin_tools import discover
        result=dict(discover(command),CPKT_TARGET_ID=target,CPKT_DEPENDENCY_BUILD_JOBS='2')
    else:
        report=command([ROOT/'scripts/cpkt-toolchains.sh','discover',target],capture=True)
        values=dict(line.split('=',1) for line in report.splitlines() if '=' in line)
        if values.get('status')!='ready':raise ValueError('missing verified toolchain; prepare '+target+' explicitly')
        result={'CPKT_TARGET_ID':target,'CPKT_DEPENDENCY_BUILD_JOBS':os.environ.get('CPKT_DEPENDENCY_BUILD_JOBS',prior.get('CPKT_DEPENDENCY_BUILD_JOBS','8')),'CMAKE_SYSROOT':values.get('sysroot','')}
        for out,key in [('CMAKE_C_COMPILER','cc'),('CMAKE_CXX_COMPILER','cxx'),('CMAKE_NM','nm'),('CMAKE_AR','ar'),('CMAKE_READELF','readelf'),('CMAKE_OTOOL','otool')]:
            if key in values:result[out]=values[key]
        for out,key in [('CPKT_CXX_STDLIB_STATIC_LIBRARY','libstdcxx_a'),('CPKT_CXX_LIBGCC_STATIC_LIBRARY','libgcc_a')]:
            if key in values:result[out]=values[key]
        if target.endswith('darwin'):
            result['CPKT_OSXCROSS_ROOT']=values['root'];result['CPKT_OSXCROSS_HOST']=values['prefix']
            sdks=sorted((Path(values['root'])/'SDK').glob('MacOSX*.sdk'),reverse=True)
            if not sdks:raise ValueError('verified osxcross toolchain has no SDK')
            result['CMAKE_OSX_SYSROOT']=str(sdks[0].resolve())
    for key in ('CMAKE_C_COMPILER','CMAKE_CXX_COMPILER','CMAKE_NM','CMAKE_AR','CMAKE_READELF','CMAKE_OTOOL','CMAKE_OSX_SYSROOT'):
        if prior.get(key) and key in result and Path(prior[key]).resolve()!=Path(result[key]).resolve():
            raise ValueError('configured '+key+' differs from verified selected toolchain; prepare core explicitly')
    jobs=os.environ.get('CPKT_DEPENDENCY_BUILD_JOBS',os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL',prior.get('CPKT_DEPENDENCY_BUILD_JOBS','2' if sys.platform=='darwin' else '8')))
    if not re.fullmatch('[1-9][0-9]*',jobs):raise ValueError('consumer job limit must be a positive integer')
    limit=2 if sys.platform=='darwin' else 8
    if int(jobs)>limit:raise ValueError('consumer job limit may not exceed '+str(limit)+' on this host')
    result['CPKT_DEPENDENCY_BUILD_JOBS']=jobs
    return dict(facade_abi_defaults(ROOT),**dict(prior,**result))


def configure_consumer(prefix,workspace,target,configured,records,data):
    source=workspace/'source';build=workspace/'build';source.mkdir(parents=True,exist_ok=True)
    lines=['cmake_minimum_required(VERSION 3.21)','project(cpkt_installed_consumer LANGUAGES C CXX)','set(CMAKE_C_STANDARD 99)','set(CMAKE_C_EXTENSIONS OFF)','set(CMAKE_C_STANDARD_REQUIRED ON)']
    packages=sorted({p for item in records.values() for p in [item['package']]+item.get('extra_packages',[])})
    for package in packages:
        directory=next((item.get('package_directory',package) for item in records.values() if item['package']==package),package)
        lines+=['set('+package+'_DIR "'+str(prefix/'lib/cmake'/directory)+'")']
        lines+=['find_package('+package+' CONFIG REQUIRED PATHS "'+str(prefix)+'" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)']
    if 'cmocka' in packages:
        lines+=['if(NOT CMOCKA_LIBRARY STREQUAL "cmocka::cmocka" OR NOT CMOCKA_LIBRARIES STREQUAL "cmocka::cmocka")',
                '  message(FATAL_ERROR "cmocka upstream discovery result variables are missing")',
                'endif()']
    if any(item['group']=='all' for item in records.values()):
        for component in data['components'].values():
            for directory in component['package']['cmake']:
                package=next((item['package'] for item in data['installed_consumers'].values() if item.get('package_directory',item['package'])==directory),directory)
                lines+=['set('+package+'_DIR "'+str(prefix/'lib/cmake'/directory)+'")',
                        'find_package('+package+' CONFIG REQUIRED PATHS "'+str(prefix)+'" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)']
    runtime=ROOT/'cmake/CpktLocalRuntime.cmake'
    lines+=['set(CPKT_LOCAL_RUNTIME_EXTRA_PATHS "'+str(prefix/'lib')+'")','include("'+str(runtime)+'")']
    if '-linux-' in target:lines+=['list(PREPEND CPKT_LOCAL_RUNTIME_LINK_OPTIONS "-Wl,-rpath,'+str(prefix/'lib')+'")']
    for name,item in records.items():
        inputs=[item['source']]+item.get('extra_sources',[])
        for input in inputs:
            destination=source/Path(input).name
            if not destination.exists():shutil.copy2(ROOT/input,destination)
        sources=' '.join('"'+Path(input).name+'"' for input in inputs)
        lines += [('add_library('+name+' SHARED '+sources+')') if item['kind']=='pic' else 'add_executable('+name+' '+sources+')',
                  'target_compile_options('+name+' PRIVATE -Wall -Wextra -Wpedantic -Werror)',
                  'target_link_libraries('+name+' PRIVATE '+' '.join(item['link'])+')']
        if item.get('compile_definitions'):lines+=['target_compile_definitions('+name+' PRIVATE '+' '.join(item['compile_definitions'])+')']
        if item.get('c_final_link'):lines+=['set_target_properties('+name+' PROPERTIES LINKER_LANGUAGE C)']
        if item['standard']==89:
            lines+=['set_source_files_properties("'+Path(item['source']).name+'" PROPERTIES COMPILE_OPTIONS "-std=c89;-pedantic-errors;-Wall;-Wextra;-Werror")']
        if name=='cpkt_cmake_lua_runtime_strict':
            lines+=['target_compile_definitions('+name+' PRIVATE CPKT_STRICT_FILE="${CMAKE_BINARY_DIR}/strict_file.lua")']
        if item['kind']!='pic':lines+=['cpkt_use_local_runtime('+name+')']
    if 'sqlite_extension_package_consumer' in records:
        shutil.copy2(ROOT/'tests/sdk-consumers/sqlite_extension_from_package.c',source/'sqlite_extension_from_package.c')
        lines+=['add_library(sqlite_extension_from_package MODULE sqlite_extension_from_package.c)',
                'target_include_directories(sqlite_extension_from_package PRIVATE "'+str(prefix/'include')+'")',
                'target_compile_options(sqlite_extension_from_package PRIVATE -Wall -Wextra -Werror)',
                'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/sqlite-module-path.txt" CONTENT "$<TARGET_FILE:sqlite_extension_from_package>")']
    lines+=['file(WRITE "${CMAKE_BINARY_DIR}/runtime-flags.txt" "${CPKT_LOCAL_RUNTIME_LINK_OPTIONS}")']
    (source/'CMakeLists.txt').write_text('\n'.join(lines)+'\n')
    args=['cmake','-S',source,'-B',build,'-G','Unix Makefiles','-DCMAKE_C_COMPILER='+configured['CMAKE_C_COMPILER'],'-DCMAKE_CXX_COMPILER='+configured['CMAKE_CXX_COMPILER'],'-DCPKT_TARGET_ID='+target,'-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY','-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF','-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF']
    if target.endswith(('gnu','musl')):
        arch,_,libc=target.split('-')
        args+=['-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/CpktReadOnlyToolchain.cmake'),'-DCPKT_TARGET_ARCH='+arch,'-DCPKT_TARGET_OS=linux','-DCPKT_TARGET_LIBC='+libc]
    elif sys.platform!='darwin':args+=['-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/toolchains/arm64-apple-darwin.cmake')]
    else:args+=['-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0','-DCMAKE_OSX_SYSROOT='+configured['CMAKE_OSX_SYSROOT']]
    selected_env={'CPKT_RESOLVED_TARGET':target}
    configure_started=time.monotonic()
    command(['bash',ROOT/'scripts/run-no-warnings.sh','installed consumer configure',*args],env=selected_env)
    print('[installed configure] target='+target+' cases='+str(len(records))+' seconds='+str(round(time.monotonic()-configure_started,3)),flush=True)
    command(['bash',ROOT/'scripts/run-no-warnings.sh','installed consumer build','cmake','--build',build,'--parallel',configured['CPKT_DEPENDENCY_BUILD_JOBS']],env=selected_env)
    # Independently retained link-closure expectations from the original smoke.
    for name,item in records.items():
        link=(build/'CMakeFiles'/f'{name}.dir/link.txt').read_text()
        for value in item.get('link_contains',[]):
            if value.replace('@prefix',str(prefix)) not in link:raise ValueError('lost static closure '+name+': '+value)
        for value in item.get('link_lacks',[]):
            if value.replace('@prefix',str(prefix)) in link:raise ValueError('unwanted linkage '+name+': '+value)
    return build


def runtime_invocation(binary,target,configured,runner):
    if '-linux-' not in target:return runner+[str(binary)],False
    output=command([configured['CMAKE_READELF'],'-l',binary],capture=True)
    match=re.search(r'Requesting program interpreter: ([^\]]+)\]',output)
    if not match:return runner+[str(binary)],False
    sysroot=Path(configured['CMAKE_SYSROOT']).resolve()
    loader=Path(match[1])
    if not loader.is_relative_to(sysroot):loader=sysroot/match[1].lstrip('/')
    if not loader.is_file() or not loader.resolve().is_relative_to(sysroot):raise ValueError('consumer loader outside verified target sysroot')
    prefix=Path(configured['CPKT_INSTALLED_PREFIX']).resolve()
    # A delivered DSO's RUNPATH can supersede the executable's inherited RPATH
    # for indirect libc dependencies. Use the verified loader and the same
    # explicit library path for inspection and execution, including under QEMU.
    libraries=os.pathsep.join(map(str,(prefix/'lib',sysroot/'lib',sysroot/'usr/lib')))
    return runner+[str(loader),'--library-path',libraries,str(binary)],True


def validate_runtime_resolution(binary,target,configured,runner,invocation=None):
    invocation,dynamic=invocation or runtime_invocation(binary,target,configured,runner)
    if not dynamic:return []
    resolution=command(invocation[:-1]+['--list',str(binary)],capture=True)
    sysroot=Path(configured['CMAKE_SYSROOT']).resolve()
    prefix=Path(configured['CPKT_INSTALLED_PREFIX']).resolve()
    delivered={p.name for p in (prefix/'lib').rglob('*') if re.search(r'\.so(?:\.|$)',p.name)}
    dependencies=[]
    for line in resolution.splitlines():
        link=re.match(r'\s*(\S+)\s+=>\s+(\S+)',line)
        if not link:continue
        name,path=link.groups()
        # musl's self-libc alias can report the ELF interpreter's guest path.
        # Resolve only that alias to the loader already selected for execution.
        if target.endswith('-musl') and name=='libc.so' and Path(path).is_absolute():
            guest_loader=sysroot/path.lstrip('/')
            selected_loader=Path(invocation[len(runner)]).resolve()
            if guest_loader.is_file() and guest_loader.resolve().is_relative_to(sysroot) and guest_loader.resolve()==selected_loader:
                path=str(selected_loader)
        if name in delivered and (not Path(path).is_file() or not Path(path).resolve().is_relative_to(prefix)):
            raise ValueError('consumer loaded a delivered library outside selected prefix: '+name+' => '+path)
        if Path(path).is_absolute() and not (Path(path).resolve().is_relative_to(prefix) or Path(path).resolve().is_relative_to(sysroot)):
            raise ValueError('consumer loaded a host runtime outside verified prefix/sysroot: '+name+' => '+path)
        dependencies.append({'name':name,'path':path})
    return dependencies


def sqlite_module_path(build):
    """Read the filename generated by CMake for the MODULE target."""
    path=Path((build/'sqlite-module-path.txt').read_text().strip())
    if not path.is_absolute() or not path.resolve().is_relative_to(build.resolve()) or not path.is_file():
        raise ValueError('installed SQLite extension MODULE output missing from consumer build: '+str(path))
    return path


def execute(binary,arguments,target,configured,failure=False,static_plugins=False):
    case={'executable':Path(binary).name,'arguments':list(map(str,arguments)),
          'executable_sha256':validator.sha(Path(binary)),
          'intentional_failure':failure,'target':target}
    if target.endswith('darwin') and sys.platform!='darwin':return dict(case,status='deferred-native-runtime',runtime='darwin-cross-compile')
    runner=[]
    if target.startswith(('aarch64','armhf')) and not target.endswith('darwin'):
        runner=[os.environ.get('CPKT_QEMU_AARCH64' if target.startswith('aarch64') else 'CPKT_QEMU_ARM','/usr/bin/qemu-aarch64' if target.startswith('aarch64') else '/usr/bin/qemu-arm'),'-L',configured['CMAKE_SYSROOT']]
        if not Path(runner[0]).is_file():raise ValueError('required Linux emulator absent: '+runner[0])
    invocation=runtime_invocation(binary,target,configured,runner)
    case['loader_resolution']=validate_runtime_resolution(binary,target,configured,runner,invocation)
    if failure:
        from cpkt_operation import operation_fds
        result=subprocess.run(invocation[0]+arguments,capture_output=True,text=True,pass_fds=operation_fds())
        if result.returncode!=1 or 'intentional failure 37' not in result.stdout+result.stderr or 'cmocka_downstream_behavior.c' not in result.stdout+result.stderr:raise ValueError('cmocka intentional failure/location semantics changed')
    else:command(invocation[0]+arguments,env={'SASL_PATH':'/cpkt-no-external-sasl-plugins' if static_plugins else str(Path(configured['CPKT_INSTALLED_PREFIX'])/'lib/sasl2')})
    return dict(case,status='passed',runtime='qemu' if runner else 'native')


def composition_records(data,groups):
    # Owned suites already cover the full API. These additional probes exercise
    # installed package composition, including static PIC and C++ closure.
    probes={
        'core':['cpkt_cmake_openssl_facade','cpkt_cmake_openssl_facade_shared','cpkt_cmake_pic_openssl_facade'],
        'db':['cpkt_cmake_postgres_facade','cpkt_cmake_sqlite_facade','cpkt_cmake_iodbc_shared','cpkt_cmake_pic_postgres_facade','cpkt_cmake_pic_sqlite_facade'],
        'misc':['cpkt_cmake_pdf_facade','cpkt_cmake_pdf_facade_shared','cpkt_cmake_pic_pdf_facade','cpkt_cmake_audio_sus_facade','cpkt_cmake_pic_audio_sus_facade','cpkt_sus_mixed_cxx']}
    owner=data['repository_group']
    if owner not in groups:raise ValueError('selected closure omits the owning SDK')
    return {name:copy.deepcopy(data['installed_consumers'][name]) for name in probes[owner]}


def darwin_link_libraries(words):
    """Keep the last shared/system library flag; retain repeated static archives.

    Apple ld warns about repeated -l flags. Static SDK inputs are already explicit
    archive paths here, and their order/repetition can be required for resolution.
    """
    seen=set();result=[]
    for word in reversed(words):
        if word.startswith('-l'):
            if word in seen:continue
            seen.add(word)
        result.append(word)
    return list(reversed(result))


def run_consumers(prefix,target,preset,groups,owners,composition=False):
    data=load(ROOT);configured=configuration(target,preset)
    records={k:v for k,v in data['installed_consumers'].items() if v['group'] in owners or v['group']=='all' and set(groups)==set(data['groups']) and set(owners)==set(data['groups'])}
    if composition:records=composition_records(data,groups)
    if not records:raise ValueError('empty installed consumer inventory')
    workspace=safe_owned(ROOT/'build/verification'/target/('all/composition-'+ '-'.join(groups) if composition else '-'.join(owners))/'installed-consumers')
    if workspace.exists():shutil.rmtree(workspace)
    workspace.mkdir(parents=True)
    original_prefix=prefix
    baseline=file_records(prefix)
    statuses=[]
    for relocation in (False,True):
        if relocation:
            moved=prefix.parent/(prefix.name+'-relocated')
            prefix.rename(moved);prefix=moved
        validator.validate(prefix,groups)
        configured['CPKT_INSTALLED_PREFIX']=str(prefix)
        if 'misc' in owners:
            whisper=next(c for c in validator.load_manifest(prefix,'misc')['components'] if c['name']=='whisper')
            if whisper['features'].get('backend_capabilities')!='cpu':raise ValueError('manifest does not record compiled CPU-only speech backend')
        phase=workspace/('relocated' if relocation else 'initial');phase.mkdir()
        inspect(prefix,target,configured,data,owners,phase)
        if 'core' in owners and not composition:
            # A cross compiler alone leaves CMake's processor at the host ABI.
            # Use the same discovery-only target setup as the linked consumers.
            toolchain=ROOT/'cmake/CpktReadOnlyToolchain.cmake' if target.endswith(('gnu','musl')) else ROOT/'cmake/toolchains/arm64-apple-darwin.cmake' if sys.platform!='darwin' else ''
            command([sys.executable,ROOT/'tests/auth_package_discovery_test.py',ROOT,'--scratch',phase,'--compiler',configured['CMAKE_C_COMPILER'],'--toolchain='+str(toolchain),'--sdk-prefix',prefix],env={'CPKT_RESOLVED_TARGET':target})
            if target.endswith('darwin') and sys.platform=='darwin':command(['bash',ROOT/'tests/darwin_curl_package_test.sh',prefix],group='core')
        build=configure_consumer(prefix,phase,target,configured,records,data)
        if 'cpkt_cmake_lua_runtime_strict' in records:
            # Preserve every original strict runtime field and module assertion.
            (build/'strict_file.lua').write_text('''local h = require('host')
local c = require('chunkmod')
if h.context ~= 'strict-context' then error('bad file context') end
if c.value ~= 'chunk' then error('bad file chunk') end
if package.path ~= 'zero/?.lua;first/?.lua' then error('bad file package path') end
if package.cpath ~= 'zero/?.so;first/?.so' then error('bad file package cpath') end
if strict_name ~= 'facade' or strict_flag ~= true or strict_count ~= 7 or strict_number ~= 2.5 then error('bad globals') end
if required_side_effect ~= 'ok' then error('bad require side effect') end
if arg[1] ~= 'one' or arg[2] ~= 'two' then error('bad argv') end
''')
        for name,item in records.items():
            if item['kind']=='pic':continue
            args=[str(build/'strict_file.lua') if a=='@strict_lua' else str(sqlite_module_path(build)) if a=='@sqlite_module' else a for a in item.get('runtime_args',[])]
            statuses.append(execute(build/name,args,target,configured))
            if item.get('intentional_failure'):statuses.append(execute(build/name,['failure'],target,configured,True))
        # Raw pkg-config usage is always preceded by the same installed validator.
        command([sys.executable,prefix/'share/cpkt/validate-sdk.py','--prefix',prefix,'--groups',','.join(groups)])
        pc_env={'PKG_CONFIG_PATH':'','PKG_CONFIG_LIBDIR':str(prefix/'lib/pkgconfig'),'PKG_CONFIG_SYSROOT_DIR':''}
        flags=(build/'runtime-flags.txt').read_text().split(';')
        flags=[x for x in flags if x]
        compile_flags=[]
        command_env={}
        if target.endswith('darwin'):
            compile_flags=['-mmacosx-version-min=15.0','-isysroot',configured['CMAKE_OSX_SYSROOT']]
            if sys.platform!='darwin':
                flags+=['--ld-path='+str(Path(configured['CPKT_OSXCROSS_ROOT'])/'bin'/(configured['CPKT_OSXCROSS_HOST']+'-ld'))]
                command_env={'LD_LIBRARY_PATH':str(Path(configured['CPKT_OSXCROSS_ROOT'])/'lib')+(':'+os.environ['LD_LIBRARY_PATH'] if os.environ.get('LD_LIBRARY_PATH') else '')}
        for name,item in records.items():
            if not item.get('pc') or item['kind']=='pic' or item['runtime_args']:continue
            for static in (False,True):
                output=command(['pkg-config']+(['--static'] if static else [])+['--cflags','--libs',item['pc'],*item.get('pc_extra',[])],capture=True,env=pc_env)
                words=shlex.split(output)+['-D'+v for v in item.get('compile_definitions',[])]
                if static and item['pc']=='cpkt-sus' and words.count('-lcpktaudio')!=1:raise ValueError('cpkt-sus static pkg-config must emit -lcpktaudio exactly once')
                if static and item['pc']=='cpkt-sus' and not target.endswith('darwin'):
                    for runtime in ('libstdc++.a','libgcc.a'):
                        if not any(w.endswith('/'+runtime) and Path(w).resolve()==(prefix/'lib/cpkt-cxx'/runtime).resolve() for w in words):raise ValueError('pkg-config omitted packaged C++ runtime '+runtime)
                if not static and item.get('c_final_link') and item.get('extra_sources'):
                    # The caller's own C++ object needs a runtime under a C final
                    # driver. Facade-only shared C callers do not acquire it.
                    if target.endswith('darwin'):words+=['-lc++']
                    else:
                        for name_runtime in ('libstdc++.a','libgcc.a'):
                            runtime=prefix/'lib/cpkt-cxx'/name_runtime
                            if not runtime.is_file():raise ValueError('missing delivered caller C++ runtime: '+name_runtime)
                            words.append(str(runtime))
                        words+=['-lm','-pthread']
                if static:
                    words=[str(prefix/'lib'/('lib'+w[2:]+'.a')) if w.startswith('-l') and (prefix/'lib'/('lib'+w[2:]+'.a')).is_file() else w for w in words]
                if target.endswith('darwin'):words=darwin_link_libraries(words)
                binary=phase/(name+'-pc-'+('static' if static else 'shared'))
                inputs=[ROOT/item['source']]
                if item.get('extra_sources'):
                    inputs=[]
                    for input in [item['source']]+item['extra_sources']:
                        cpp=Path(input).suffix in ('.cpp','.cc','.cxx')
                        obj=phase/(name+'-pc-'+Path(input).name+'.o')
                        command([configured['CMAKE_CXX_COMPILER' if cpp else 'CMAKE_C_COMPILER'],'-std=c++11' if cpp else '-std=c89','-Wall','-Wextra','-Werror','-c',ROOT/input,'-o',obj]+compile_flags+[w for w in words if w.startswith(('-I','-D'))],env=command_env)
                        inputs.append(obj)
                arguments=[configured['CMAKE_C_COMPILER'],'-std=c'+str(item['standard']),'-Wall','-Wextra','-Werror']+(['-pedantic-errors'] if item['standard']==89 else [])+inputs+['-o',binary]+compile_flags+flags+words
                if target.endswith('musl') and static:arguments+=['-static']
                if target.endswith('darwin'):arguments+=['-mmacosx-version-min=15.0','-Wl,-rpath,'+str(prefix/'lib')]
                command(['bash',ROOT/'scripts/run-no-warnings.sh','installed pkg-config consumer',*arguments],env=command_env)
                statuses.append(execute(binary,[],target,configured))
        if 'core' in owners and target.endswith('gnu') and not composition:
            item=next(v for v in records.values() if v.get('pc')=='cpkt-sasl' and v['kind']!='pic')
            words=shlex.split(command(['pkg-config','--static','--cflags','--libs','cpkt-sasl'],capture=True,env=pc_env))
            binary=phase/'sasl-fully-static'
            command([configured['CMAKE_C_COMPILER'],'-std=c89','-pedantic-errors','-Wall','-Wextra','-Werror','-static',ROOT/item['source'],'-o',binary]+words)
            headers=command([configured['CMAKE_READELF'],'-l',binary],capture=True)
            dynamic=command([configured['CMAKE_READELF'],'-d',binary],capture=True)
            if 'INTERP' in headers or '(NEEDED)' in dynamic:raise ValueError('fully static SASL consumer has a dynamic runtime closure')
            statuses.append(execute(binary,[],target,configured,static_plugins=True))
        from cpkt_sdk_examples import run_examples
        if not composition:statuses.extend(run_examples(prefix,target,configured,data,owners,phase,execute))
        if file_records(prefix)!=baseline:raise ValueError('consumer verification modified delivered SDK bytes/modes')
    if prefix != original_prefix:prefix.rename(original_prefix)
    return statuses


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--composition',action='store_true')
    parser.add_argument('--prefix',type=Path,required=True);parser.add_argument('--target',required=True)
    parser.add_argument('--groups',required=True);parser.add_argument('--owners',required=True);parser.add_argument('--preset',required=True)
    args=parser.parse_args();owners=args.owners.split(',');groups=args.groups.split(',')
    scope='all' if args.composition else owners[0] if len(owners)==1 else 'all'
    if not set(owners)<=set(groups):parser.error('consumer owners must be installed selected groups')
    if 'CPKT_OPERATION_FD' not in os.environ:return locked_run(ROOT,scope,[sys.executable,__file__]+sys.argv[1:])
    delegated(ROOT,scope)
    print(json.dumps(run_consumers(args.prefix,args.target,args.preset,groups,owners,args.composition)))


if __name__=='__main__':
    try:sys.exit(main())
    except (ValueError,OSError,RuntimeError,KeyError) as error:sys.exit('installed consumer: '+str(error))
