#!/usr/bin/env python3
"""Inspect all real pinned recipe plans, without downloads or compilation."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
sys.path.insert(0,str(root/'scripts'))
from cpkt_inventory import load, components_for
data = load(root)
top = (root/'CMakeLists.txt').read_text()
pins = top[top.index('set(CPKT_BUNDLE_VERSION'):top.index('set(CPKT_DOWNLOAD_ROOT')]
discovery = subprocess.run([str(root/'scripts/cpkt-toolchains.sh'),'discover','x86_64-linux-gnu'],capture_output=True,text=True,check=True)
tools = dict(line.split('=',1) for line in discovery.stdout.splitlines() if '=' in line)
with tempfile.TemporaryDirectory(prefix='real-recipe-plans-',dir=root/'build') as temporary:
    source = Path(temporary)/'source'; source.mkdir()
    shutil.copytree(root/'cmake',source/'cmake')
    shutil.copytree(root/'src',source/'src')
    if (root/'vendor').is_dir():shutil.copytree(root/'vendor',source/'vendor')
    main = '''cmake_minimum_required(VERSION 3.21)
project(real_recipe_plans C CXX)
find_package(Threads REQUIRED)
set(CPKT_BUILD_DEPENDENCIES ON)
set(CPKT_TARGET_ID x86_64-linux-gnu)
set(CPKT_TARGET_ARCH x86_64)
set(CPKT_TARGET_OS linux)
set(CPKT_TARGET_LIBC gnu)
set(CPKT_DEPENDENCY_BUILD_TYPE Release)
set(CPKT_DEPENDENCY_BUILD_JOBS 2)
set(CPKT_SUS_CPU_ONLY ON)
set(CPKT_TOOLCHAIN_ROOT "ROOT")
set(CPKT_EXTERNAL_ROOT "${CMAKE_BINARY_DIR}/deps")
set(CPKT_DEPENDENCY_BUILD_ROOT "${CMAKE_BINARY_DIR}/deps-build")
set(CPKT_DOWNLOAD_ROOT "${CMAKE_BINARY_DIR}/downloads")
include(cmake/CpktDependencies.cmake)
# Only transport acquisition is stubbed. The actual recipe and ExternalProject
# commands/targets are retained. No plan in this fixture is executed.
macro(cpkt_cached_external_project_add)
  ExternalProject_Add(${ARGV})
endmacro()
'''.replace('"ROOT"','"'+tools['root']+'"') + pins
    for component in components_for(data,'all'):
        main += 'get_property(_before GLOBAL PROPERTY CPKT_DEPENDENCY_TARGETS)\n'
        main += data['components'][component]['recipe_functions'][-1]+'()\n'
        main += '''get_property(_after GLOBAL PROPERTY CPKT_DEPENDENCY_TARGETS)
set(_owned "${_after}")
if(_before)
  list(REMOVE_ITEM _owned ${_before})
endif()
add_custom_target(cpkt_deps_COMPONENT DEPENDS ${_owned})
'''.replace('COMPONENT',component)
    (source/'CMakeLists.txt').write_text(main)
    for generator in ('Ninja','Unix Makefiles'):
        binary = Path(temporary)/generator.replace(' ','-')
        arguments=['cmake','-S',str(source),'-B',str(binary),'-G',generator]
        for key,name in [('cc','C_COMPILER'),('cxx','CXX_COMPILER'),('ar','AR'),('ranlib','RANLIB'),('ld','LINKER'),('strip','STRIP'),('nm','NM'),('objcopy','OBJCOPY'),('objdump','OBJDUMP'),('addr2line','ADDR2LINE'),('readelf','READELF')]:
            arguments.append('-DCMAKE_'+name+'='+tools[key])
        result=subprocess.run(arguments,capture_output=True,text=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        for component in components_for(data,'all'):
            if generator=='Ninja':
                result=subprocess.run(['ninja','-C',str(binary),'-t','commands','cpkt_deps_'+component],capture_output=True,text=True,check=True)
                plan=result.stdout
            else:
                # Make dry-run can try recursive upstream makes on absent source.
                # Its generated owning target file is the static plan authority.
                graph=(binary/'CMakeFiles/Makefile2').read_text()
                edges={}
                for owner,dependency in re.findall(r'^(CMakeFiles/[^ :]+\.dir/all): (CMakeFiles/[^ :]+\.dir/all)$',graph,re.M):
                    edges.setdefault(owner,[]).append(dependency)
                pending=['CMakeFiles/cpkt_deps_'+component+'.dir/all'];visited=set();pieces=[]
                while pending:
                    owner=pending.pop()
                    if owner in visited:continue
                    visited.add(owner);pending.extend(edges.get(owner,[]))
                    pieces.append(owner)
                    rules=binary/owner.removesuffix('/all')/'build.make'
                    if rules.is_file():pieces.append(rules.read_text())
                plan='\n'.join(pieces)
            recipe=(root/'cmake/CpktDependencies.cmake').read_text()
            function=data['components'][component]['recipe_functions'][-1]
            body=re.search(r'function\('+function+r'\).*?endfunction\(\)',recipe,re.S).group(0)
            projects=re.findall(r'(?:cpkt_cached_external_project_add|cpkt_external_project_add)\((cpkt_[a-z0-9_]+)',body)
            projects += re.findall(r'set\(project_name (cpkt_[a-z0-9_]+)\)',body)
            for project in set(projects):
                if project not in plan:raise RuntimeError(component+' lacks variant producer '+project+' in '+generator)
            if generator in ('Ninja','Unix Makefiles'):
                allowed=set()
                def add(name):
                    allowed.add(data['components'][name]['directory'])
                    for dependency in data['components'][name]['dependencies']:add(dependency)
                add(component)
                for name,item in data['components'].items():
                    if item['directory'] not in allowed and str(binary/'deps-build'/item['directory'])+'/' in plan:
                        raise RuntimeError(component+' plan leaks unrelated '+name)
        print(generator+': all real component plans retain their static/shared producer closure')
    # The real generated C89 facade commands keep every declared native header
    # dependency; consumer graphs import these files rather than producer stamps.
    for family,headers in {'NGHTTP2':['nghttp2.h','nghttp2ver.h'],'LIBSSH2':['libssh2.h','libssh2_sftp.h','libssh2_publickey.h'],
                           'MQTTC':['mqtt.h'],'LUA':['lua.h','luaconf.h','lauxlib.h','lualib.h']}.items():
        component={'NGHTTP2':'nghttp2','LIBSSH2':'libssh2','MQTTC':'mqttc','LUA':'lua'}[family]
        if component not in components_for(data,'all'):continue
        declaration=re.search(r'set\(CPKT_'+family+r'_FACADE_NATIVE_HEADERS\b.*?\)',top,re.S)
        if not declaration or any(header not in declaration.group(0) for header in headers):
            raise RuntimeError(family+' generator lost native header dependencies')
        if '${CPKT_'+family+'_FACADE_NATIVE_HEADERS}' not in top:
            raise RuntimeError(family+' native header inventory is not used by its generator')
