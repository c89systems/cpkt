#!/usr/bin/env python3
"""Supported group backend for Make and direct selected development operations."""
import argparse
import json
import os
from pathlib import Path
import shutil
import re
import subprocess
import sys
import time

from cpkt_inventory import validate_inputs, GROUPS, REPOSITORY_GROUP, load, components_for, record
from cpkt_presets import preset_info as resolve_preset
from cpkt_operation import delegated, run as locked_run, child_delegation
from cpkt_receipts import (cache, canonical, component_input_id, component_receipt,
                           digest, group_outputs, producer_dir, publish, readiness_path,
                           read, tree_identity, validate_component, validate_core, validate_development,
                           verification_inputs, file_identity, publish_component)

ROOT = Path(__file__).resolve().parent.parent
CMAKE = os.environ.get('CMAKE', 'cmake')
CTEST = os.environ.get('CTEST', 'ctest')


def owned_path(path):
    path = Path(path)
    if not path.is_relative_to(ROOT):
        raise RuntimeError('owned mutation escaped repository: ' + str(path))
    for parent in (path, *path.parents):
        if parent == ROOT:
            break
        if parent.is_symlink():
            raise RuntimeError('owned mutation has a symlink ancestor: ' + str(parent))
    return path


def preset_info(preset):
    return resolve_preset(ROOT, preset)

def binary_dir(preset, group):
    _, target, configuration = preset_info(preset)
    return ROOT / 'build' / target / (REPOSITORY_GROUP if group=='all' else group) / configuration


def event(phase, group, target, started, status, **fields):
    path = ROOT / 'build/control/events.jsonl'
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a') as stream:
        stream.write(json.dumps(dict(run=os.environ.get('CPKT_OPERATION_RUN'), phase=phase,
                     group=group, target=target, seconds=time.monotonic()-started,
                     status=status, **fields), sort_keys=True) + '\n')


def command(arguments, group, target, phase, capture=False):
    started = time.monotonic()
    with child_delegation(ROOT,group) as (env,fds):
        if os.environ.get('CPKT_PACKAGE_ACTIVE')=='1':
            from cpkt_package_command import run
            label='configure' if 'configure' in phase else 'build' if 'build' in phase else 'test' if 'test' in phase else 'fixture'
            try:result=run(arguments,root=ROOT,phase=label,env=env,pass_fds=fds,capture=capture)
            except SystemExit as error:
                event(phase,group,target,started,'failed',command=list(map(str,arguments)),exit_status=error.code)
                raise
            event(phase,group,target,started,'passed',command=list(map(str,arguments)))
            return result
        result = subprocess.run(list(map(str, arguments)), cwd=ROOT, pass_fds=fds,env=env,
                                text=True, capture_output=capture)
    event(phase, group, target, started, 'passed' if result.returncode == 0 else 'failed', command=list(map(str, arguments)))
    if result.returncode < 0:sys.exit(128-result.returncode)
    if result.returncode:
        if capture:
            print(result.stdout + result.stderr, file=sys.stderr)
        raise RuntimeError(phase + ' failed with status ' + str(result.returncode))
    return result.stdout if capture else ''


def environment(preset, group):
    _, target, _ = preset_info(preset)
    os.environ.update(GROUP=group, CPKT_PRESET=preset, CPKT_RESOLVED_TARGET=target)


def native_darwin_configuration(group,target):
    from cpkt_darwin_tools import discover
    return discover(lambda args, capture=False:
                    command(args,group,target,'native-darwin-tool-discovery',capture))


def configure_command(preset, group, directory, producer=False):
    item, target, configuration = preset_info(preset)
    arguments = [CMAKE, '--preset', preset, '-B', directory, '-DCPKT_GROUP=' + group,
                 '-DCPKT_DEPENDENCY_PRODUCER=' + ('ON' if producer else 'OFF'),
                 '-DCPKT_BUILD_DEPENDENCIES=' + ('ON' if producer else 'OFF'),
                 '-DCPKT_PREREQUISITE_CONFIGURATION=' + ('Debug' if preset in ('fuzz','opcua-fuzz','valgrind') else configuration)]
    if target.endswith('darwin') and sys.platform == 'darwin':
        arguments += ['-D'+key+':'+('PATH' if key=='CMAKE_OSX_SYSROOT' else 'FILEPATH')+'='+value
                      for key,value in native_darwin_configuration(group,target).items()]
        for variable in ('CPKT_DARWIN_HOST_MIG','CPKT_DARWIN_HOST_MIGCOM','CPKT_OTOOL','CMAKE_OTOOL'):
            if os.environ.get(variable):
                arguments += ['-D' + variable + ':FILEPATH=' + os.environ[variable]]
    if producer:
        arguments += ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCPKT_BUILD_TESTS=OFF',
                      '-DCPKT_FACADE_ONLY=OFF', '-DCPKT_ENABLE_FUZZING=OFF']
        arguments += ['-D'+key+'='+value for key,value in producer_flags(preset,group).items()]
    if target.endswith(('-gnu', '-musl')) and preset not in ('fuzz', 'opcua-fuzz'):
        arguments += ['-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'cmake/CpktReadOnlyToolchain.cmake')]
    elif preset in ('fuzz', 'opcua-fuzz'):
        arguments += ['-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'cmake/CpktReadOnlyAflToolchain.cmake')]
    if preset in ('fuzz', 'opcua-fuzz', 'valgrind'):
        arguments += ['-DCPKT_BORROW_ORDINARY_DEPENDENCIES=ON']
    # Environment, existing cache and preset overrides retain the configured limit.
    existing = cache(Path(directory) / 'CMakeCache.txt') if (Path(directory) / 'CMakeCache.txt').is_file() else {}
    configured = item['cacheVariables'].get('CPKT_DEPENDENCY_BUILD_JOBS', '2' if sys.platform == 'darwin' else '8')
    if isinstance(configured, dict):
        configured = configured['value']
    jobs = str(os.environ.get('CPKT_DEPENDENCY_BUILD_JOBS', os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL', existing.get('CPKT_DEPENDENCY_BUILD_JOBS', configured))))
    if not jobs.isdecimal() or int(jobs) < 1:
        raise RuntimeError('configured job limit must be a positive integer')
    limit=2 if sys.platform=='darwin' else 8
    if int(jobs)>limit:raise RuntimeError('configured job limit exceeds '+str(limit)+' for this host')
    if os.environ.get('CPKT_SOURCE_RECONSTRUCTION')=='1':
        generator=os.environ.get('CPKT_SOURCE_GENERATOR','Ninja')
        if generator not in ('Ninja','Unix Makefiles'):raise RuntimeError('unsupported source reconstruction generator')
        arguments += ['-G',generator,'-DCPKT_DEPENDENCY_CACHE='+os.environ['CPKT_DEPENDENCY_CACHE']]
    arguments += ['-DCPKT_DEPENDENCY_BUILD_JOBS=' + jobs]
    return arguments


def prepare(preset, group, dependency=None):
    environment(preset, group)
    _, target, configuration = preset_info(preset)
    data = load(ROOT)
    if group in ('db', 'misc'):
        validate_core(ROOT, target, configuration, preset)
    elif target.endswith(('-gnu', '-musl')):
        report=command([ROOT / 'scripts/cpkt-toolchains.sh', 'discover', target], group, target, 'toolchain-discovery', True)
        if 'status=ready' not in report.splitlines():
            command([ROOT / 'scripts/cpkt-toolchains.sh', 'ensure', target], group, target, 'toolchain-preparation')
    names = components_for(data, group)
    if dependency:
        if dependency not in data['components'] or data['components'][dependency]['group'] != group:
            raise RuntimeError('DEPENDENCY=' + dependency + ' does not belong to GROUP=' + group)
        needed = set()
        def add(name):
            if data['components'][name]['group'] != group:
                return
            needed.add(name)
            for child in data['components'][name]['dependencies']:
                add(child)
        add(dependency)
        names = [name for name in names if name in needed]
    directory = producer_dir(ROOT, target, group)
    if (directory/'CMakeCache.txt').is_file() and requested_producer_change(preset,group,directory):
        # Presets override cached values during configure. Apply those settings
        # before comparing receipts, rather than validating the previous request.
        command(configure_command(preset,group,directory,True),group,target,'producer-request-refresh')
    stale = []
    for name in names:
        try:
            validate_component(ROOT, target, name)
            event('component', group, target, time.monotonic(), 'reused', component=name, reason='matching successful inputs and output content')
        except (RuntimeError, OSError, KeyError) as error:
            stale.append(name)
            event('component', group, target, time.monotonic(), 'required', component=name, reason=str(error))
    if not stale:
        return
    # Revoke readiness before configure can refresh any component output.
    for path in (ROOT / 'build/verification' / target / group).glob('*-development.json'):
        path.unlink()
    for name in stale:
        component_receipt(ROOT, target, name).unlink(missing_ok=True)
        item = data['components'][name]
        for base in ('deps', 'deps-build'):
            owned = ROOT / '.cache' / base / target / item['directory']
            for ancestor in (owned, *owned.parents):
                if ancestor == ROOT:
                    break
                if ancestor.is_symlink():
                    raise RuntimeError('owned component root contains symlink: ' + str(ancestor))
            if owned.exists():
                shutil.rmtree(owned)
    directory = producer_dir(ROOT, target, group)
    owned_path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    command(configure_command(preset, group, directory, True), group, target, 'producer-configure')
    jobs = cache(directory / 'CMakeCache.txt')['CPKT_DEPENDENCY_BUILD_JOBS']
    # A producer graph owns both variants. Build only the affected component
    # targets; its transitive prerequisites are in this same graph.
    command([CMAKE, '--build', directory, '--parallel', jobs, '--target']
            + ['cpkt_deps_' + name for name in stale], group, target, 'producer-build')
    for name in names:
        # The real producer publishes each component after both installs; keep
        # this fallback for small standalone producer fixtures using this backend.
        if not component_receipt(ROOT, target, name).exists():
            publish_component(ROOT, target, name)
        validate_component(ROOT, target, name)


def preset_cache_value(value,preset):
    if isinstance(value,dict):value=value['value']
    if isinstance(value,bool):value='TRUE' if value else 'FALSE'
    item,_,_=preset_info(preset)
    macros={'sourceDir':str(ROOT),'sourceParentDir':str(ROOT.parent),
            'sourceDirName':ROOT.name,'presetName':preset,'generator':item.get('generator','Ninja'),
            'hostSystemName':os.uname().sysname,'dollar':'$','pathListSep':os.pathsep}
    def expand(text,visited):
        def replace(match):
            if match[1]:
                if match[1] not in macros:raise RuntimeError('unsupported cache macro: '+match[1])
                return macros[match[1]]
            key=match[3]
            environment=item.get('environment',{})
            if match[2]=='penv' or key not in environment:return os.environ.get(key,'')
            if key in visited:raise RuntimeError('cyclic preset environment: '+key)
            if environment[key] is None:return ''
            return expand(str(environment[key]),visited|{key})
        return re.sub(r'\$\{([^}]+)\}|\$(p?env)\{([^}]+)\}',replace,text)
    return expand(str(value),set())


def producer_flags(preset,group):
    item,_,_=preset_info(preset)
    consumer=binary_dir(preset,group)/'CMakeCache.txt'
    previous=cache(consumer) if consumer.is_file() else {}
    producer=producer_dir(ROOT,preset_info(preset)[1],group)/'CMakeCache.txt'
    produced=cache(producer) if producer.is_file() else {}
    result={}
    for key,environment_key in (('CMAKE_C_FLAGS','CFLAGS'),('CMAKE_CXX_FLAGS','CXXFLAGS')):
        value=item['cacheVariables'].get(key)
        if key=='CMAKE_CXX_FLAGS' and value is None and key not in previous and key not in produced:
            continue  # Do not introduce a C++ cache variable into a C-only graph.
        result[key]=preset_cache_value(value,preset) if value is not None else previous.get(key,preset_cache_value('$env{'+environment_key+'}',preset))
    for key in ('CMAKE_EXE_LINKER_FLAGS','CMAKE_SHARED_LINKER_FLAGS',
                'CMAKE_MODULE_LINKER_FLAGS','CMAKE_STATIC_LINKER_FLAGS'):
        value=item['cacheVariables'].get(key)
        fallback='' if key=='CMAKE_STATIC_LINKER_FLAGS' else preset_cache_value('$env{LDFLAGS}',preset)
        result[key]=preset_cache_value(value,preset) if value is not None else previous.get(key,fallback)
    return result


def requested_producer_change(preset,group,directory):
    item,_,_=preset_info(preset)
    existing=cache(directory/'CMakeCache.txt')
    managed={'CMAKE_BUILD_TYPE','CPKT_BUILD_TESTS','CPKT_FACADE_ONLY',
             'CPKT_ENABLE_FUZZING','CPKT_BUILD_DEPENDENCIES','CPKT_GROUP',
             'CPKT_DEPENDENCY_PRODUCER','CPKT_PREREQUISITE_CONFIGURATION',
             'CPKT_DEPENDENCY_BUILD_JOBS','CMAKE_EXPORT_COMPILE_COMMANDS'}
    def equivalent(left,right):
        truth={'on':True,'true':True,'yes':True,'1':True,
               'off':False,'false':False,'no':False,'0':False}
        if left.lower() in truth and right.lower() in truth:
            return truth[left.lower()]==truth[right.lower()]
        return left==right
    flags=producer_flags(preset,group)
    _,target,_=preset_info(preset)
    if target.endswith('darwin') and sys.platform=='darwin':
        flags.update(native_darwin_configuration(group,target))
    variables=dict(item['cacheVariables'],**flags)
    for key,value in variables.items():
        if key in managed or value is None:continue
        value=value if key in flags else preset_cache_value(value,preset)
        if not equivalent(value,existing.get(key,'')):return True
    return False


def configure(preset, group, fresh=False, prepare_outputs=True):
    environment(preset, group)
    _, target, configuration = preset_info(preset)
    directory = binary_dir(preset, group)
    owned_path(directory)
    if preset in ('fuzz', 'opcua-fuzz', 'valgrind'):
        validate_development(ROOT, target, group, 'Debug', 'debug')
        if group!='core':validate_core(ROOT, target, 'Debug', 'debug')
    elif prepare_outputs:
        prepare(preset, group)
    elif group in ('db', 'misc'):
        validate_core(ROOT, target, configuration, preset)
    if fresh and directory.exists():
        shutil.rmtree(directory)
    command(configure_command(preset, group, directory), group, target, 'consumer-configure')
    return directory


def build(preset, group, targets=None, fresh=False):
    _, target, configuration = preset_info(preset)
    previous = None
    if not fresh:
        try:
            previous = validate_development(ROOT, target, group, configuration, preset)
        except RuntimeError:
            pass
    readiness_path(ROOT, target, group, configuration).unlink(missing_ok=True)
    directory = configure(preset, group, fresh)
    configured = cache(directory / 'CMakeCache.txt')
    arguments = [CMAKE, '--build', directory, '--parallel', configured['CPKT_DEPENDENCY_BUILD_JOBS']]
    if targets:
        arguments += ['--target'] + targets
    command(arguments, group, target, 'consumer-build')
    if not targets:
        publish(ROOT / 'build/verification' / target / group / (configuration + '-built.json'),
                {'kind': 'built', 'target': target, 'group': group, 'configuration': configuration,
                 'outputs': group_outputs(directory), 'coverage': []})
    if previous and previous['outputs'] == group_outputs(directory) and previous['verification_id'] == verification_inputs(ROOT, group, configured):
        current = {name: validate_component(ROOT, target, name)['input_id']
                   for name in components_for(load(ROOT), group, True)}
        if current == previous['components']:
            publish(readiness_path(ROOT, target, group, configuration), previous, preserve_run=True)
    return directory


def test(preset, group, regex=None, label=None, fresh=False):
    _, target, configuration = preset_info(preset)
    readiness_path(ROOT, target, group, configuration).unlink(missing_ok=True)
    directory = build(preset, group, fresh=fresh)
    _, target, configuration = preset_info(preset)
    readiness_path(ROOT, target, group, configuration).unlink(missing_ok=True)
    inventory = json.loads(command([CTEST, '--test-dir', directory, '--show-only=json-v1'], group, target, 'test-inventory', True))
    tests = inventory['tests']
    if not tests:
        raise RuntimeError('required selected test inventory is empty')
    expected = []
    for item in tests:
        owner = record(load(ROOT)['tests'], item['name'])['group']
        if owner not in (group, 'tooling', 'all'):
            raise RuntimeError('cross-group test registered: ' + item['name'])
        properties = {p['name']: p['value'] for p in item.get('properties', [])}
        if properties.get('DISABLED'):
            raise RuntimeError('required selected test disabled: ' + item['name'])
        for argument in item.get('command', []):
            path = Path(argument)
            if path.is_absolute() and str(path).startswith(str(directory) + '/') and not path.exists():
                # Commands also contain generated fixture output filenames; only
                # executable inventory (first command / emitted target paths) is required.
                if argument == item['command'][0] or argument in (directory / 'cpkt-owned-outputs.txt').read_text().splitlines():
                    raise RuntimeError('required executable absent: ' + argument)
        expected.append(item['name'])
    junit = directory / 'cpkt-test-results.xml'
    junit.unlink(missing_ok=True)
    arguments = [CTEST, '--test-dir', directory, '--no-tests=error', '--output-on-failure',
                 '--output-junit', junit]
    if regex:
        arguments += ['-R', regex]
    if label:
        arguments += ['-L', label]
    configured = cache(directory / 'CMakeCache.txt')
    os.environ['CPKT_CONFIGURED_BINARY_DIR'] = str(directory)
    os.environ['CPKT_CONFIGURED_GROUP'] = group
    command(arguments, group, target, 'group-tests')
    import xml.etree.ElementTree as ET
    results = ET.parse(junit).getroot()
    actual = [item.attrib['name'] for item in results.iter('testcase')]
    if any(item.find('skipped') is not None or item.find('failure') is not None
           or item.find('error') is not None for item in results.iter('testcase')):
        raise RuntimeError('skipped/failed/partial required coverage cannot supply readiness')
    if not regex and not label and sorted(actual) != sorted(expected):
        raise RuntimeError('required CTest cases missing from actual results')
    if regex or label:
        return  # Focused proof is never promoted to full development readiness.
    profile = 'osxcross' if target.endswith('darwin') and sys.platform != 'darwin' else (
              'native-darwin' if sys.platform == 'darwin' else 'native-linux' if target == 'x86_64-linux-gnu' else 'linux-runner')
    publish(readiness_path(ROOT, target, group, configuration),
            {'kind': 'development', 'target': target, 'group': group,
             'configuration': configuration, 'profile': profile,
             'verification_id': verification_inputs(ROOT, group, configured),
             'components': {name: validate_component(ROOT, target, name)['input_id']
                            for name in components_for(load(ROOT), group, True)},
             'outputs': group_outputs(directory), 'coverage': expected,
            'deferred': ['native-runtime-required-release'] if profile == 'osxcross' else []})


def composition_tests(directory, target):
    junit = directory/'cpkt-test-results.xml'
    junit.unlink(missing_ok=True)
    os.environ.update(CPKT_CONFIGURED_BINARY_DIR=str(directory),CPKT_CONFIGURED_GROUP='all')
    command([CTEST,'--test-dir',directory,'--no-tests=error','--output-on-failure',
             '--output-junit',junit],'all',target,'tooling-and-composition-tests')
    import xml.etree.ElementTree as ET
    results=ET.parse(junit).getroot()
    expected=(directory/'cpkt-required-coverage.txt').read_text().splitlines()
    actual=[item.attrib['name'] for item in results.iter('testcase')]
    if not expected or sorted(actual) != sorted(expected) or any(item.find('skipped') is not None
            or item.find('failure') is not None or item.find('error') is not None for item in results.iter('testcase')):
        raise RuntimeError('partial/skipped/failed all-group composition coverage')


def memcheck(group, regex=None):
    if sys.platform != 'linux' or not shutil.which('valgrind'):
        raise RuntimeError('selected Memcheck requires native Linux with host Valgrind')
    command(['bash',ROOT/'scripts/require-native-hardening-host.sh','valgrind'],group,'x86_64-linux-gnu','memcheck-host-check')
    _, target, configuration = preset_info('valgrind')
    receipt = ROOT/'build/verification'/target/group/(configuration+('-memcheck-focused.json' if regex else '-memcheck.json'))
    receipt.unlink(missing_ok=True)
    directory = build('valgrind', group)
    selection = ['-R',regex] if regex else []
    inventory = json.loads(command([CTEST,'--test-dir',directory,'--show-only=json-v1','-L','memcheck']+selection,group,target,'memcheck-inventory',True))
    expected = [item['name'] for item in inventory['tests']]
    if not expected:
        raise RuntimeError('selected group has no supported Memcheck cases: ' + group)
    junit = directory/'cpkt-memcheck-results.xml'
    junit.unlink(missing_ok=True)
    suppression=load(ROOT)['groups'][group].get('hardening',{}).get('memcheck_suppression')
    suppression_options=['--overwrite','MemoryCheckSuppressionFile='+str(ROOT/suppression)] if suppression else []
    command([CTEST,'--test-dir',directory,'-T','memcheck','-L','memcheck','--no-tests=error',
             '--stop-on-failure','--output-on-failure','--output-junit',junit,'--overwrite',
             'MemoryCheckCommandOptions=--error-exitcode=1 --leak-check=full --track-origins=yes --show-leak-kinds=definite,indirect',
             ]+suppression_options+selection,group,target,'group-memcheck')
    import xml.etree.ElementTree as ET
    results = ET.parse(junit).getroot()
    actual = [item.attrib['name'] for item in results.iter('testcase')]
    if sorted(actual) != sorted(expected) or any(item.find('failure') is not None or item.find('error') is not None or item.find('skipped') is not None for item in results.iter('testcase')):
        raise RuntimeError('partial/failed Memcheck cannot supply success')
    tag = (directory/'Testing/TAG').read_text().splitlines()[0]
    memory = ET.parse(directory/'Testing'/tag/'DynamicAnalysis.xml').getroot()
    if any(int(item.text or '0') for item in memory.iter('Defect')):
        raise RuntimeError('Valgrind reported memory defects')
    configured = cache(directory/'CMakeCache.txt')
    publish(receipt,{'kind':'memcheck','group':group,'target':target,'configuration':configuration,
        'profile':'native-memcheck','coverage':actual,'selection':regex,'verification_id':verification_inputs(ROOT,group,configured),
        'outputs':group_outputs(directory),'valgrind':file_identity(Path(shutil.which('valgrind')).resolve()),
        'components':{name:validate_component(ROOT,target,name)['input_id'] for name in components_for(load(ROOT),group,True)}})


def clean(group, dist_only=False):
    def remove(path):
        owned_path(path)
        if path.is_symlink():
            path.unlink()
        elif path.exists():
            shutil.rmtree(path) if path.is_dir() else path.unlink()
    if dist_only:
        if group != 'all':
            raise RuntimeError('clean dist requires GROUP=all')
        remove(ROOT / 'dist')
        return
    if REPOSITORY_GROUP=='db' and group in ('all', 'db') and (ROOT / 'build/devenv').exists():
        command(['bash', ROOT / 'scripts/devenv.sh', 'down'], group, 'native', 'devenv-teardown')
        remove(ROOT / 'build/devenv')
    if group == 'all':
        for path in (ROOT / 'build').iterdir():
            if path.name != 'control':
                remove(path)
        for path in (ROOT / 'build/control').iterdir():
            if path.name == 'tmp':
                for child in path.iterdir():
                    if child.name != os.environ.get('CPKT_OPERATION_RUN'):
                        remove(child)
            elif path.name not in ('operation.lock', 'reserved-tag.json', '.operation.sock'):
                remove(path)
        remove(ROOT / '.cache')
        remove(ROOT / 'dist')
        for path in ROOT.glob('package-assertions-*'):remove(path)
        for source_directory in ('scripts','tests','tools','cmake'):
            for path in (ROOT/source_directory).rglob('__pycache__'):
                remove(path)
    else:
        data = load(ROOT)
        for target in (ROOT / 'build').iterdir():
            if target.is_dir() and target.name not in ('control', 'verification', 'package-stage'):
                remove(target / group)
        for base in ('verification', 'package-stage'):
            for target in (ROOT / 'build' / base).glob('*'):
                remove(target / group)
        for base in ('deps', 'deps-build'):
            for target in (ROOT / '.cache' / base).glob('*'):
                for name in components_for(data, group):
                    remove(target / data['components'][name]['directory'])
        for target in (ROOT / '.cache/dependency-contracts').glob('*'):
            for name in components_for(data, group):
                remove(target / (name + '.txt'))
        # Optional evidence references missing core by content; no rewrite needed.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('configure', 'build', 'test', 'memcheck', 'deps', 'clean', 'path', 'inventory', 'validate-core', 'preflight', 'composition'))
    parser.add_argument('--group', choices=GROUPS, default=os.environ.get('GROUP', 'all'))
    parser.add_argument('--preset', default=os.environ.get('PRESET', 'debug'))
    parser.add_argument('--fresh', action='store_true')
    parser.add_argument('--target', action='append')
    parser.add_argument('--dependency')
    parser.add_argument('--regex')
    parser.add_argument('--label')
    parser.add_argument('--dist-only', action='store_true')
    args = parser.parse_args()
    if args.dist_only and (args.action != 'clean' or args.group != 'all'):
        parser.error('--dist-only requires clean --group all')
    if args.target and args.action != 'build':
        parser.error('--target requires the build action')
    if args.dependency and args.action != 'deps':
        parser.error('--dependency requires the deps action')
    if args.fresh and args.action not in ('configure','build'):
        parser.error('--fresh requires configure or build')
    if args.regex and args.action not in ('test','memcheck'):
        parser.error('--regex requires test or memcheck')
    if args.label and args.action != 'test':
        parser.error('--label requires test')
    if args.group not in GROUPS:
        parser.error('unknown GROUP: ' + args.group)
    if args.action == 'memcheck' and args.preset not in ('debug','valgrind'):
        parser.error('Memcheck supports the native debug/valgrind preset only')
    data = load(ROOT)
    if args.action not in ('clean','path','inventory'):validate_inputs(ROOT,data,args.group)
    selected_targets = {group: [] for group in GROUPS}
    for name in args.target or []:
        owner = record(data['targets'], name)['group']
        owner = REPOSITORY_GROUP if owner == 'tooling' else owner
        if owner != REPOSITORY_GROUP:
            raise RuntimeError('target ' + name + ' belongs to GROUP=' + owner)
        selected_targets[owner].append(name)
    explicit_group = os.environ.get('GROUP')
    if explicit_group and explicit_group not in ('all', args.group):
        raise RuntimeError('conflicting GROUP environment and --group selector')
    _, target, configuration = preset_info(args.preset)
    if args.action == 'path':
        print(binary_dir(args.preset, args.group))
        return 0
    if args.action == 'inventory':
        print(json.dumps(data))
        return 0
    if args.action == 'validate-core':
        delegated(ROOT, args.group)
        validate_core(ROOT, target, configuration, args.preset)
        return 0
    if args.action == 'preflight':
        # It intentionally does not provision or configure an SDK producer.
        if 'CPKT_OPERATION_FD' not in os.environ:
            return locked_run(ROOT, args.group, [sys.executable, __file__] + sys.argv[1:])
        environment(args.preset, args.group)
        item, _, _ = preset_info(args.preset)
        toolchain = (ROOT / 'cmake/CpktReadOnlyToolchain.cmake' if target.endswith(('-gnu','-musl'))
                     else Path(item['toolchainFile'].replace('${sourceDir}',str(ROOT))) if 'toolchainFile' in item else None)
        arguments = [sys.executable, ROOT/'scripts/cpkt_preflight.py','--root',ROOT,
                     '--group',args.group,'--target',target,'--preset',args.preset]
        if toolchain:
            arguments += ['--toolchain',toolchain]
        command(arguments,args.group,target,'standalone-preflight')
        return 0
    if args.preset in ('fuzz','opcua-fuzz') and REPOSITORY_GROUP == 'db':
        raise RuntimeError('db fuzz is unsupported; select core/fuzz or misc/opcua-fuzz')
    if args.dependency and args.dependency not in data['components']:
        raise RuntimeError('unknown dependency: ' + args.dependency)
    if args.dependency and args.group != 'all' and data['components'][args.dependency]['group'] != args.group:
        raise RuntimeError('dependency belongs to another group: ' + args.dependency)
    if 'CPKT_OPERATION_FD' not in os.environ:
        return locked_run(ROOT, args.group, [sys.executable, __file__] + sys.argv[1:])
    delegated(ROOT, args.group)
    if args.action == 'composition' and args.group != 'all':
        parser.error('composition requires --group all')
    if args.action == 'clean':
        clean(args.group, args.dist_only)
        return 0
    group = REPOSITORY_GROUP
    if args.action == 'composition':
        raise RuntimeError('composition is verified against packaged artifacts; use make package-verify')
    if args.action == 'configure':
        configure(args.preset, group, args.fresh)
    elif args.action == 'build':
        build(args.preset, group, args.target, args.fresh)
    elif args.action == 'test':
        test(args.preset, group, args.regex, args.label)
    elif args.action == 'deps':
        prepare(args.preset, group, args.dependency)
    elif args.action == 'memcheck':
        memcheck(group, args.regex)

    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, OSError, KeyError, ValueError) as error:
        print('group-build: ' + str(error), file=sys.stderr)
        sys.exit(2)
