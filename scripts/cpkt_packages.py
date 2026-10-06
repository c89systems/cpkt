#!/usr/bin/env python3
"""Inventory-driven SDK artifact validation and composition evidence."""
import argparse
import ast
from collections import Counter
import fnmatch
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
import signal
import zipfile

from cpkt_inventory import load, components_for, GROUPS, REPOSITORY_GROUP
from cpkt_lock import delegated, operation_fds, child_delegation
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
        phase='configure' if str(args[0])=='cmake' and '--build' not in args else 'build' if '--build' in args else 'fixture' if any('preflight' in str(a) for a in args) else 'test' if any('consumer' in str(a) or 'build.sh' in str(a) for a in args) else 'package'
        process=subprocess.Popen(['bash',str(ROOT/'scripts/package-command.sh'),str(ROOT),phase,'--',*map(str,args)],
            cwd=cwd,env=delegation,pass_fds=fds,text=True,
            stdout=subprocess.PIPE if capture else None,stderr=subprocess.PIPE if capture else None)
        handlers={}
        received=[]
        def forward(signum,frame):
            received.append(signum)
            try:process.send_signal(signum)
            except ProcessLookupError:pass
        try:
            for signum in (signal.SIGHUP,signal.SIGINT,signal.SIGTERM):
                handlers[signum]=signal.signal(signum,forward)
            output,error=process.communicate()
            if process.returncode or received:
                if capture:print((output or '')+(error or ''),file=sys.stderr)
                raise SystemExit(128+received[0] if received else process.returncode)
            return output or ''
        finally:
            for signum,handler in handlers.items():signal.signal(signum,handler)


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
    # Fingerprint only tools used for this target. Native Darwin can legitimately
    # cache CMAKE_READELF-NOTFOUND and has no packaged GNU C++ runtime archives.
    tool_keys={'CMAKE_C_COMPILER','CMAKE_CXX_COMPILER','CMAKE_NM','CMAKE_AR'}
    if target.endswith('darwin'):tool_keys.add('CMAKE_OTOOL')
    else:tool_keys.update(('CMAKE_READELF','CPKT_CXX_STDLIB_STATIC_LIBRARY','CPKT_CXX_LIBGCC_STATIC_LIBRARY'))
    tools={k:file_identity(Path(v).resolve()) for k,v in settings.items() if k in tool_keys and v}
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
    parser=argparse.ArgumentParser(description='Structured SDK artifact and evidence validation; Bash owns workflows')
    parser.add_argument('action',choices=('assert-inputs','verify-selected','compose','checksums','verify-checksums','verify-artifacts','invalidate','invalidate-selected'))
    parser.add_argument('--group',default='all',choices=GROUPS)
    parser.add_argument('--preset')
    parser.add_argument('--scope',choices=('selected','binary','release'),default='binary')
    parser.add_argument('--version')
    parser.add_argument('--base',type=Path)
    parser.add_argument('--fresh-owned',action='store_true')
    args=parser.parse_args()
    delegated(ROOT,args.group)
    ver=args.version or version()
    current_run=os.environ.get('CPKT_RELEASE_PRODUCTION')=='1'
    if args.preset:
        os.environ['CPKT_PRESET']=args.preset
        _,target,configuration=preset_info(ROOT,args.preset)
        if configuration!='Release':raise ValueError('SDK artifact validation requires Release')
    if args.action=='invalidate':
        invalidate_release(ver);return
    if args.action=='invalidate-selected':
        if not args.preset or args.group=='all':raise ValueError('selected invalidation requires preset and owner')
        proof_path(target,args.group).unlink(missing_ok=True);return
    if args.action=='assert-inputs':
        if not args.preset or args.group=='all':raise ValueError('input validation requires preset and owner')
        directory=ROOT/'build'/target/args.group/configuration
        configured=cache(directory/'CMakeCache.txt')
        proof=read(readiness_path(ROOT,target,args.group,configuration))
        if proof['verification_id']!=verification_inputs(ROOT,args.group,configured) or proof['outputs']!=group_outputs(directory):raise ValueError('package input is not the tested graph')
        for component in components_for(load(ROOT),args.group):validate_component(ROOT,target,component)
        return
    if args.action=='verify-selected':
        if not args.preset or args.group=='all':raise ValueError('selected validation requires preset and owner')
        verify_selected(args.preset,args.group,ver);return
    if args.action=='compose':
        if not args.preset or args.group!='all':raise ValueError('composition requires one preset and aggregate owner')
        combinations(args.preset,ver,args.base or ROOT/'dist',reuse_owned=not args.fresh_owned)
        return
    if args.action in ('checksums','verify-checksums','verify-artifacts') and args.group=='all':
        safe_owned(ROOT/'build/verification'/args.scope/ver/'proof.json').unlink(missing_ok=True)
    if args.action=='verify-checksums':
        if args.scope=='selected':
            if args.group=='all' or not args.preset:raise ValueError('selected checksums require preset and owner')
            snapshot=ROOT/'build/verification'/target/args.group/'CHECKSUMS'
            base=stage_dir(target,args.group)/'archives'
        else:
            if args.group!='all' or args.preset:raise ValueError('aggregate checksums reject narrowing')
            snapshot=ROOT/'dist'/f'cpkt-{ver}-CHECKSUMS' if args.scope=='release' else ROOT/'build/verification/binary'/ver/'CHECKSUMS'
            base=ROOT/'dist'
        check_snapshot(snapshot,base,ver,args.scope,args.group,target if args.preset else None,current_run=current_run)
        return
    if args.action=='checksums':
        snapshot,base=checksum_snapshot(ver,args.scope,args.group,target if args.preset else None,current_run=current_run)
        check_snapshot(snapshot,base,ver,args.scope,args.group,target if args.preset else None,current_run=current_run);return
    if args.group!='all' or args.preset:raise ValueError('aggregate validation rejects narrowing')
    base=ROOT/'dist'
    snapshot=base/f'cpkt-{ver}-CHECKSUMS' if args.scope=='release' else ROOT/'build/verification/binary'/ver/'CHECKSUMS'
    check_snapshot(snapshot,base,ver,args.scope,current_run=current_run)
    privacy([snapshot]+[base/name for name in artifacts(ver,args.scope)])
    write_json(ROOT/'build/verification'/args.scope/ver/'proof.json',dict(schema_version=1,status='passed',kind='artifact-'+args.scope,scope=args.scope,run=os.environ['CPKT_OPERATION_RUN'],manifest_sha256=sha(snapshot),artifacts=check_snapshot(snapshot,base,ver,args.scope,current_run=current_run)))


if __name__=='__main__':
    try:sys.exit(main())
    except (ValueError,RuntimeError,OSError,KeyError,TypeError) as error:sys.exit('SDK artifact validation: '+str(error))
