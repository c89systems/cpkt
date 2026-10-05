"""Execute inventory-owned examples from delivered SDK files, including pkg-config."""
from pathlib import Path
import shlex
from cpkt_packages import ROOT, command

def run_examples(prefix,target,configured,data,owners,phase,execute):
    statuses=[]
    for relative,item in data['installed_examples'].items():
        if item['group'] not in owners:continue
        delivered=prefix/'share/doc/cpkt'/item['group']/relative
        if not (delivered/'CMakeLists.txt').is_file():raise ValueError('missing installed example '+relative)
        directory=phase/"examples with spaces and apostrophe's"/Path(relative).name;source=directory/'wrapper';source.mkdir(parents=True)
        build=directory/'build'
        text='cmake_minimum_required(VERSION 3.21)\nproject(installed_example C CXX)\n'
        for component in data['components'].values():
            if component['group'] not in ('core',item['group']):continue
            for name in component['package']['cmake']+[f['cmake'] for f in component['package']['facades']]:
                text+='set('+name+'_DIR "'+str(prefix/'lib/cmake'/name)+'")\n'
        text+='set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)\nset(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)\n'
        text+='add_subdirectory("'+str(delivered)+'" example)\n'
        text+='target_compile_options('+item['target']+' PRIVATE -Werror)\n'
        text+='set(CPKT_LOCAL_RUNTIME_EXTRA_PATHS "'+str(prefix/'lib')+'")\ninclude("'+str(ROOT/'cmake/CpktLocalRuntime.cmake')+'")\n'
        if '-linux-' in target:text+='list(PREPEND CPKT_LOCAL_RUNTIME_LINK_OPTIONS "-Wl,-rpath,'+str(prefix/'lib')+'")\n'
        text+='cpkt_use_local_runtime('+item['target']+')\n'
        text+='file(WRITE "${CMAKE_BINARY_DIR}/runtime-flags.txt" "${CPKT_LOCAL_RUNTIME_LINK_OPTIONS}")\n'
        (source/'CMakeLists.txt').write_text(text)
        args=['cmake','-S',source,'-B',build,'-G','Ninja','-DCMAKE_C_COMPILER='+configured['CMAKE_C_COMPILER'],'-DCMAKE_CXX_COMPILER='+configured['CMAKE_CXX_COMPILER'],'-DCPKT_TARGET_ID='+target,'-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY']
        if '-linux-' in target:args+=['-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/CpktReadOnlyToolchain.cmake')]
        elif __import__('sys').platform!='darwin':args+=['-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/toolchains/arm64-apple-darwin.cmake')]
        else:args+=['-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0']
        env={'CPKT_RESOLVED_TARGET':target}
        command(['bash',ROOT/'scripts/run-no-warnings.sh','installed example configure',*args],env=env)
        command(['bash',ROOT/'scripts/run-no-warnings.sh','installed example build','cmake','--build',build,'--parallel',configured['CPKT_DEPENDENCY_BUILD_JOBS']],env=env)
        arguments=[str(build/'example'/value[1:]) if value.startswith('@') else value for value in item['runtime_args']]
        statuses.append(execute(build/'example'/item['target'],arguments,target,configured))
        if item['pkg_config_script']:
            flags=[value for value in (build/'runtime-flags.txt').read_text().split(';') if value]
            output=directory/(item['target']+'-pkg')
            compile_flags=['-Werror'];link_flags=flags
            if target.endswith('musl'):link_flags+=['-static']
            if target.endswith('darwin'):
                compile_flags+=['-mmacosx-version-min=15.0'];link_flags+=['-mmacosx-version-min=15.0','-Wl,-rpath,'+str(prefix/'lib')]
                if __import__('sys').platform!='darwin':
                    linker=command([configured['CMAKE_C_COMPILER'],'-print-prog-name=ld'],capture=True).strip()
                    if not Path(linker).is_file():raise ValueError('selected Darwin linker unavailable')
                    link_flags+=['--ld-path='+linker]
            command(['bash',ROOT/'scripts/run-no-warnings.sh','installed pkg-config example',delivered/item['pkg_config_script'],output,*link_flags],env={'CC':configured['CMAKE_C_COMPILER'],'CPKT_SDK_PREFIX':str(prefix),'CPKT_EXAMPLE_CFLAGS':shlex.join(compile_flags),'PKG_CONFIG_PATH':'','PKG_CONFIG_LIBDIR':str(prefix/'lib/pkgconfig')})
            statuses.append(execute(output,arguments,target,configured))
    return statuses
