"""Versioned content-based completion and development evidence."""
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import subprocess
import ast
import shlex
from functools import lru_cache

from cpkt_inventory import load, components_for, record


def digest(value):
    return hashlib.sha256(value).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode()


def file_identity(path):
    path = Path(path)
    if path.is_symlink():
        return {'type': 'symlink', 'target': os.readlink(path)}
    if not path.is_file():
        raise RuntimeError('outputs missing/corrupt: ' + str(path))
    return {'type': 'file', 'sha256': digest(path.read_bytes()), 'mode': path.stat().st_mode & 0o777}


def tree_identity(root):
    root = Path(root)
    if not root.is_dir():
        raise RuntimeError('outputs missing/corrupt: ' + str(root))
    result = {p.relative_to(root).as_posix(): file_identity(p)
              for p in sorted(root.rglob('*')) if p.is_file() or p.is_symlink()}
    if not result:
        raise RuntimeError('incomplete installed output: ' + str(root))
    for relative, item in result.items():
        if item['type'] == 'symlink' and not (root / relative).exists():
            raise RuntimeError('dangling installed output: ' + str(root / relative))
    return result


def cache(path):
    return {line.split(':', 1)[0]: line.split('=', 1)[1]
            for line in Path(path).read_text().splitlines()
            if ':' in line and '=' in line and not line.startswith(('#', '//'))}


def component_inputs(root, target, name, configuration_cache):
    # CMake owns recipe, feature and dependency-graph identities. Content
    # verification additionally binds the actual tools and runtime/header bytes.
    contract=root/'.cache/dependency-contracts'/target/(name+'.txt')
    text=contract.read_text()
    if not re.match(r'^sha256:[a-f0-9]{64}\n',text):
        raise RuntimeError('missing/unknown native component contract: '+name)
    if digest(text.split('\n',1)[1].encode()) != text.split('\n',1)[0][7:]:
        raise RuntimeError('corrupt native component contract: '+name)
    inputs={'native_contract':digest(text.encode()),'target':target}
    toolchain=configuration_cache.get('CMAKE_TOOLCHAIN_FILE')
    if toolchain:
        directory=Path(toolchain).parent
        inputs['toolchain_helpers']={p.name:file_identity(p) for p in sorted(directory.glob('*common*.cmake'))}
        if 'CpktToolchainDiscovery.cmake' in Path(toolchain).read_text():
            inputs['toolchain_helpers']['CpktToolchainDiscovery.cmake']=file_identity(directory/'CpktToolchainDiscovery.cmake')
    resolver=root/'scripts/cpkt-toolchains.sh'
    if resolver.is_file():inputs['toolchain_resolver']=file_identity(resolver)
    variables=('CMAKE_C_COMPILER','CMAKE_CXX_COMPILER','CMAKE_AR','CMAKE_LINKER',
               'CMAKE_RANLIB','CMAKE_STRIP','CMAKE_NM','CMAKE_OBJCOPY','CMAKE_OBJDUMP',
               'CMAKE_READELF','CMAKE_TOOLCHAIN_FILE','CMAKE_OTOOL','CPKT_OTOOL',
               'CMAKE_INSTALL_NAME_TOOL','CPKT_DARWIN_HOST_MIG','CPKT_DARWIN_HOST_MIGCOM',
               'CPKT_CXX_STDLIB_STATIC_LIBRARY','CPKT_CXX_LIBGCC_STATIC_LIBRARY')
    inputs['tools']={key:file_identity(Path(value).resolve())
                    for key in variables if (value:=configuration_cache.get(key,'')) and Path(value).is_file()}
    inputs['runtime_inputs']=tool_runtime_inputs(configuration_cache.get('CMAKE_SYSROOT',''),configuration_cache.get('CMAKE_OSX_SYSROOT',''))
    if target.endswith('darwin'):
        inputs['compiler_backends']=darwin_backend_inputs(configuration_cache)
    return digest(canonical(inputs))


@lru_cache(maxsize=16)
def tool_runtime_inputs(sysroot, sdk):
    # Immutable shared collections are read once per validation process, never
    # accepted by path/version alone. Include public sysroot inputs and runtime
    # libraries; compiled component output is still checked on every use.
    result = {}
    for value in (sysroot, sdk):
        if not value:
            continue
        root = Path(value)
        directories = ('include','usr/include','lib','usr/lib')
        if value == sdk:
            directories += ('System/Library/Frameworks', 'System/Library/PrivateFrameworks',
                            'System/Library/SubFrameworks', 'Library/Frameworks')
            for name in ('SDKSettings.json', 'SDKSettings.plist'):
                if (root/name).is_file():
                    result[value+'/'+name] = file_identity(root/name)
        for directory in directories:
            path = root/directory
            if path.is_dir():
                result[value+'/'+directory] = tree_identity(path) if any(
                    p.is_file() or p.is_symlink() for p in path.rglob('*')) else {}
    return result


@lru_cache(maxsize=32)
def darwin_backend_paths(compiler, language, environment):
    """Ask the selected driver which compiler/resources it actually uses."""
    result = subprocess.run([compiler, '-###', '-Wno-unused-command-line-argument',
                             '-fsyntax-only', '-x', language, '-'],
                            input='', text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError('Darwin compiler identity query failed: '+result.stderr)
    for line in result.stderr.splitlines():
        try:
            arguments = shlex.split(line)
        except ValueError:
            continue
        if '-cc1' not in arguments or '-resource-dir' not in arguments:
            continue
        compiler_path = Path(arguments[0]).resolve()
        resource_path = Path(arguments[arguments.index('-resource-dir')+1]).resolve()
        if compiler_path.is_file() and resource_path.is_dir():
            return compiler_path, resource_path
    raise RuntimeError('Darwin driver did not expose its compiler/resource identity: '+compiler)


def darwin_backend_inputs(configured):
    """Hash the host Clang behind osxcross as well as its builtin headers."""
    result = {}
    environment = tuple(os.environ.get(key, '') for key in
                        ('PATH', 'SDKROOT', 'OSXCROSS_COMPILER_PATH', 'OSXCROSS_SDKROOT'))
    for key, language in (('CMAKE_C_COMPILER','c'), ('CMAKE_CXX_COMPILER','c++')):
        compiler = configured.get(key)
        if not compiler:
            continue
        backend, resources = darwin_backend_paths(compiler, language, environment)
        result[key] = {'compiler_path':str(backend), 'compiler':file_identity(backend),
                       'resource_path':str(resources), 'resources':tree_identity(resources)}
    return result


def producer_dir(root, target, group):
    return root / 'build' / target / group / 'producer'


def component_input_id(root, target, name):
    item = load(root)['components'][name]
    configured = cache(producer_dir(root, target, item['group']) / 'CMakeCache.txt')
    return component_inputs(root, target, name, configured)


def component_receipt(root, target, name):
    item = load(root)['components'][name]
    return root / 'build/verification' / target / item['group'] / ('component-' + name + '.json')


def read(path):
    try:
        result = json.loads(Path(path).read_text())
    except (OSError, ValueError) as error:
        raise RuntimeError('successful receipt absent/malformed: ' + str(path)) from error
    if result.get('schema_version') != 1 or result.get('status') != 'passed':
        raise RuntimeError('unsuccessful/unknown receipt: ' + str(path))
    return result


def publish(path, record, preserve_run=False):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    record = dict(record, schema_version=1, status='passed',
                  run=record['run'] if preserve_run else os.environ['CPKT_OPERATION_RUN'])
    fd, name = tempfile.mkstemp(prefix='.receipt-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(canonical(record))
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)
    return record


def validate_component(root, target, name):
    item = load(root)['components'][name]
    receipt = read(component_receipt(root, target, name))
    if (receipt.get('kind') != 'component' or receipt.get('component') != name
            or receipt.get('target') != target or receipt.get('group') != item['group']
            or receipt.get('configuration') != 'ordinary'
            or set(receipt.get('coverage', [])) != {v + '-install' for v in item['variants']}):
        raise RuntimeError(name + ': incomplete/wrong producer receipt')
    if receipt['input_id'] != component_input_id(root, target, name):
        raise RuntimeError(name + ': build inputs changed')
    installed = root / '.cache/deps' / target / item['directory'] / 'install'
    if receipt['outputs'] != tree_identity(installed):
        raise RuntimeError(name + ': outputs missing/corrupt')
    return receipt


def publish_component(root, target, name):
    item = load(root)['components'][name]
    return publish(component_receipt(root, target, name),
        {'kind': 'component', 'group': item['group'], 'component': name, 'target': target,
         'configuration': 'ordinary', 'input_id': component_input_id(root, target, name),
         'outputs': tree_identity(root / '.cache/deps' / target / item['directory'] / 'install'),
         'coverage': [v + '-install' for v in item['variants']]})


def verification_inputs(root, group, configured):
    data = load(root)
    # Source/header and fixture/helper closure is explicit in the inventory.
    files = set()
    for kind in ('targets', 'tests'):
        for name, item in data[kind].items():
            if item['group'] in (group,'tooling'):
                files.update(item.get('command_inputs', []))
                files.update(item.get('source_inputs', []))
                files.update(item.get('helper_inputs', []))
    files.update(data['groups'][group].get('verification_inputs', []))
    hardening=data['groups'][group].get('hardening',{})
    if hardening.get('memcheck_suppression'):files.add(hardening['memcheck_suppression'])
    files.update(hardening.get('fuzz_seeds',[]))
    identities = {}
    pending = sorted(files)
    while pending:
        name = pending.pop()
        if name in identities or '${' in name:
            continue
        path = root / name
        if not path.is_file():
            raise RuntimeError('verification input missing: ' + name)
        if name == 'cmake/CpktDependencies.cmake':
            # Recipe-inspection fixtures read this shared file. Their relevant
            # source closure is the selected components, not unrelated recipes.
            identities[name] = {'components': {component: component_input_id(root,configured['CPKT_TARGET_ID'],component)
                for component in components_for(data,group,True)}}
            continue
        if name == 'CMakeLists.txt':
            identities[name] = file_identity(path)
            continue
        if name == 'cmake/components.json':
            identities[name] = {'group':{k:v for k,v in data['groups'][group].items() if k != 'package'},
                'records': {kind:{key:value for key,value in data[kind].items() if value['group'] in (group,'tooling')}
                            for kind in ('targets','tests')}}
            continue
        identities[name] = file_identity(path)
        if path.suffix in ('.py', '.sh', '.cmake'):
            # Inventory declares entrypoints; follow literal repo helper references.
            for child in re.findall(r'(?:scripts|tools|tests|cmake)/[A-Za-z0-9_./-]+\.(?:py|sh|cmake|hpp|h|cpp|cxx|cc|c|json|patch|series|txt)(?![A-Za-z0-9_.])', path.read_text()):
                if (root / child).is_file():
                    pending.append(child)
            if path.suffix == '.py':
                for node in ast.walk(ast.parse(path.read_text())):
                    modules = ([node.module] if isinstance(node,ast.ImportFrom) and node.module else
                               [item.name for item in node.names] if isinstance(node,ast.Import) else [])
                    for module in modules:
                        for directory in (path.parent,root/'scripts',root/'tools'):
                            child=directory/(module.replace('.','/')+'.py')
                            if child.is_file():
                                pending.append(child.relative_to(root).as_posix())
    records = {kind: {name: item for name, item in data[kind].items() if item['group'] == group}
               for kind in ('targets', 'tests')}
    abi_keys={'core':('CMOCKA','NGHTTP2','LIBSSH2','MQTTC','OPENSSL','LUA','LUA_RUNTIME','GSSAPI','SASL'), 'db':('POSTGRES','SQLITE'), 'misc':('OPCUA','PDF','AUDIO','SUS')}
    flags = {k: v for k, v in configured.items() if k in { 'CPKT_'+name+'_ABI_VERSION' for name in abi_keys[group]} or k.startswith(('CMAKE_C_FLAGS','CMAKE_CXX_FLAGS')) or k in
             ('CMAKE_CROSSCOMPILING_EMULATOR', 'CMAKE_BUILD_TYPE', 'CPKT_TARGET_ID', 'CPKT_BUILD_TESTS')}
    helpers = {}
    top = (root / 'CMakeLists.txt').read_text()
    for name in ('cpkt_add_repo_warning_errors', 'cpkt_configure_c89_target',
                 'cpkt_apply_auth_export_catalog', 'cpkt_configure_c89_lua_native_header_target',
                 'cpkt_add_lua_runtime_mock_test', 'cpkt_register_local_runtime_checks'):
        found = re.search(r'function\(' + name + r'\b.*?endfunction\(\)', top, re.S)
        if found:
            helpers[name] = found.group(0)
    for name in ('cmake/CpktLocalRuntime.cmake','cmake/CpktGroups.cmake','cmake/CpktTestInventory.cmake'):
        common = root/name
        if common.is_file():
            helpers[name] = file_identity(common)
    environment = {key: os.environ.get(key, '') for key in data['groups'][group].get('verification_environment', [])}
    directory=root/'build'/configured['CPKT_TARGET_ID']/group/configured['CMAKE_BUILD_TYPE']
    definitions=file_identity(directory/'CTestTestfile.cmake') if (directory/'CTestTestfile.cmake').is_file() else {}
    tools = {}
    for key in ('CMAKE_C_COMPILER','CMAKE_CXX_COMPILER','CMAKE_NM','CMAKE_CROSSCOMPILING_EMULATOR'):
        value = configured.get(key,'').split(';')[0]
        if value and Path(value).is_file():
            tools[key] = file_identity(Path(value).resolve())
    version_script = root/'scripts/release-version.sh'
    version = subprocess.check_output([str(version_script),str(root)],text=True).strip() if version_script.exists() else ''
    return digest(canonical({'records': records, 'files': identities, 'flags': flags, 'definitions': definitions, 'helpers': helpers, 'environment': environment,'tools':tools,'version':version}))


def group_outputs(directory):
    manifest = directory / 'cpkt-owned-outputs.txt'
    paths = [Path(line) for line in manifest.read_text().splitlines() if line]
    paths += [p for p in (directory / 'generated').rglob('*') if p.is_file() or p.is_symlink()]
    exports = directory / 'cpkt-facades.cmake'
    if exports.is_file():
        paths.append(exports)
    return {str(p.relative_to(directory)): file_identity(p) for p in sorted(set(paths))}


def readiness_path(root, target, group, configuration):
    return root / 'build/verification' / target / group / (configuration + '-development.json')


def validate_development(root, target, group, configuration, preset):
    try:
        path = readiness_path(root, target, group, configuration)
        receipt = read(path)
        if (receipt.get('kind') != 'development' or not receipt.get('coverage')
                or receipt.get('group') != group or receipt.get('target') != target
                or receipt.get('configuration') != configuration):
            raise RuntimeError(group+' is not development-ready')
        directory = root / 'build' / target / group / configuration
        configured = cache(directory / 'CMakeCache.txt')
        required = directory / 'cpkt-required-coverage.txt'
        if not required.is_file():
            raise RuntimeError(group+' required test inventory is absent')
        if sorted(receipt['coverage']) != sorted(required.read_text().splitlines()):
            raise RuntimeError(group+' required coverage is incomplete')
        if receipt['verification_id'] != verification_inputs(root, group, configured):
            raise RuntimeError(group+' verification changed')
        components = {name: validate_component(root, target, name)['input_id']
                      for name in components_for(load(root), group, True)}
        if components != receipt.get('components'):
            raise RuntimeError(group+' consumed component identities changed')
        if receipt['outputs'] != group_outputs(directory):
            raise RuntimeError(group+' facade outputs missing/corrupt')
        return receipt
    except (RuntimeError, OSError, KeyError) as error:
        raise RuntimeError(group+' development proof for ' + target + '/' + configuration + ': ' + str(error)
                           + '\nRepair: make test GROUP=' + group + ' PRESET=' + preset) from error


def validate_core(root, target, configuration, preset):
    return validate_development(root, target, 'core', configuration, preset)
