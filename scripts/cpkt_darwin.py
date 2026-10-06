#!/usr/bin/env python3
"""Native source and exact producer-artifact lanes remain distinct evidence."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

from cpkt_packages import ROOT, command, version,  verify_selected, selected_archive, combinations, archive_name, prefix_name, safe_extract, validator, write_json, sha, invalidate_release, privacy
from cpkt_lock import delegated, ensure_operation as locked_run
from cpkt_inventory import REPOSITORY_GROUP
from cpkt_receipts import read
from cpkt_packages import safe_owned


def extract_smoke(archive_path,destination):
    """Preflight the whole ZIP before writing any members or following links."""
    from cpkt_packages import safe_owned
    destination=safe_owned(destination)
    with zipfile.ZipFile(archive_path) as archive:
        records={}
        for item in archive.infolist():
            name=item.filename.rstrip('/')
            validator.path_name(name)
            if name.split('/')[0]!='darwin-smoke-test' or name in records:raise ValueError('invalid/duplicate smoke ZIP path')
            mode=item.external_attr>>16;kind=mode & 0o170000
            if not item.is_dir() and kind not in (0,0o100000,0o120000):raise ValueError('unsupported smoke ZIP member type')
            if kind==0o120000:
                target=archive.read(item).decode()
                normalized=os.path.normpath(str(Path(name).parent/target))
                if not target or Path(target).is_absolute() or '\\' in target or normalized.split('/')[0]!='darwin-smoke-test':raise ValueError('unsafe smoke symlink')
            records[name]=item
        for name in records:
            safe_owned(destination/name)
            for parent in Path(name).parents:
                if str(parent) in records and records[str(parent)].external_attr>>16 & 0o170000==0o120000:raise ValueError('smoke ZIP symlink ancestor')
            if (destination/name).exists() or (destination/name).is_symlink():raise ValueError('smoke ZIP destination collision')
        destination.mkdir(parents=True,exist_ok=True)
        for name,item in records.items():
            path=destination/name;mode=item.external_attr>>16
            if item.is_dir():path.mkdir(parents=True,exist_ok=True);continue
            path.parent.mkdir(parents=True,exist_ok=True)
            if mode & 0o170000==0o120000:path.symlink_to(archive.read(item).decode())
            else:path.write_bytes(archive.read(item));path.chmod(mode & 0o777)


def write_smoke_archive(package,destination):
    """Preserve delivered modes/links in a deterministic compressed ZIP."""
    from cpkt_packages import safe_owned
    package=safe_owned(package);destination=safe_owned(destination)
    with zipfile.ZipFile(destination,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for path in sorted(package.rglob('*')):
            if path.is_dir() and not path.is_symlink():continue
            relative='darwin-smoke-test/'+path.relative_to(package).as_posix()
            info=zipfile.ZipInfo(relative,(1980,1,1,0,0,0));info.create_system=3
            info.external_attr=path.lstat().st_mode<<16
            info.compress_type=zipfile.ZIP_DEFLATED
            archive.writestr(info,os.readlink(path).encode() if path.is_symlink() else path.read_bytes(),compresslevel=6)


def smoke_zip(ver,base=None):
    target='arm64-apple-darwin';base=base or ROOT/'dist'
    workspace=safe_owned(ROOT/'build/package-stage'/target/'all/smoke')
    package=safe_owned(workspace/'darwin-smoke-test')
    extraction=safe_owned(workspace/'sdk')
    destination=safe_owned(base/f'cpkt-{ver}-{target}-smoke-test.zip')
    workspace.mkdir(parents=True,exist_ok=True)
    if package.exists():shutil.rmtree(package)
    (package/'bin').mkdir(parents=True);(package/'lib').mkdir()
    if extraction.exists():shutil.rmtree(extraction)
    for group in (REPOSITORY_GROUP,):
        prefix=safe_extract(base/archive_name(ver,target,group),extraction,prefix_name(ver,target))
    ids=validator.validate(prefix,['core'] if REPOSITORY_GROUP=='core' else ['core',REPOSITORY_GROUP],ver,target)
    for source in (prefix/'lib').iterdir():
        if source.is_symlink():
            (package/'lib'/source.name).symlink_to(os.readlink(source))
        elif source.suffix=='.dylib':shutil.copy2(source,package/'lib'/source.name)
    preset='arm64-apple-darwin-native' if sys.platform=='darwin' else target+'-release'
    from cpkt_presets import preset_info
    _,_,configuration=preset_info(ROOT,preset)
    graph=ROOT/'build'/target/REPOSITORY_GROUP/configuration
    # These existing ABI binaries already have the shipped relative loader policy.
    for name in ('cpkt_abi_smoke_shared','cpkt_abi_smoke_static'):
        binary=graph/name
        if not binary.is_file():raise ValueError('missing real Darwin smoke executable: '+str(binary))
        shutil.copy2(binary,package/'bin'/name)
    write_json(package/'packages.json',ids)
    invalidate_release(ver)
    write_smoke_archive(package,destination)
    privacy([destination])
    return destination


def source_evidence():
    # Bash has completed the native source/runtime/installed-consumer workflow.
    # Record its actual tool and archive evidence without rebuilding anything.
    from cpkt_receipts import cache
    directory=ROOT/'build/arm64-apple-darwin'/REPOSITORY_GROUP/'Release'
    configured=cache(directory/'CMakeCache.txt')
    commit=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
    proof=read(ROOT/'build/verification/arm64-apple-darwin'/REPOSITORY_GROUP/'Release-development.json')
    composition=read(ROOT/'build/verification/arm64-apple-darwin/all/packages/proof.json')
    if proof['run']!=os.environ['CPKT_OPERATION_RUN'] or composition['run']!=proof['run']:raise ValueError('stale native source/composition proof')
    write_json(ROOT/'build/darwin-source-evidence.json',{'schema_version':1,'status':'passed','kind':'native-source','commit':commit,'platform':os.uname().release,'compiler':command([configured['CMAKE_C_COMPILER'],'--version'],capture=True),'sdk':command(['xcrun','--show-sdk-version'],capture=True).strip(),'deployment_floor':'15.0','jobs':int(configured.get('CPKT_DEPENDENCY_BUILD_JOBS','2')),'coverage':[proof['coverage']],'combinations':[r['order'] for r in composition['combinations']]})


def sdk_input():
    if sys.platform!='darwin' or os.uname().machine!='arm64':raise ValueError('artifact runtime proof requires actual native arm64 Darwin')
    base=Path(os.environ.get('CPKT_DARWIN_ARTIFACT_DIR',str(ROOT/'build/darwin-artifact-input')))
    from cpkt_github_handoff import validate_handoff
    handoff=validate_handoff(validator.decode((base/'handoff.json').read_bytes()))
    commit=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
    if commit!=handoff['producer_commit'] or os.environ.get('CPKT_EXPECTED_PRODUCER_COMMIT',commit)!=commit:raise ValueError('artifact lane checked-out producer commit mismatch')
    before={}
    for name,item in handoff['assets'].items():
        if '-arm64-apple-darwin' in name or name.endswith('-CHECKSUMS'):
            if sha(base/name)!=item['sha256'] or (base/name).stat().st_size!=item['size']:raise ValueError('artifact input bytes changed: '+name)
        if '-arm64-apple-darwin' in name:before[name]=sha(base/name)
    write_json(ROOT/'build/darwin-artifact-input-evidence.json',{'base':str(base),'handoff':handoff,'archives':before,'run':os.environ['CPKT_OPERATION_RUN']})
    print(base)


def sdk_smoke():
    destination=safe_owned(ROOT/'build/darwin-artifact-smoke')
    value=validator.decode((ROOT/'build/darwin-artifact-input-evidence.json').read_bytes())
    if value['run']!=os.environ['CPKT_OPERATION_RUN']:raise ValueError('stale Darwin artifact input proof')
    if destination.exists():shutil.rmtree(destination)
    extract_smoke(Path(value['base'])/f'cpkt-{value["handoff"]["version"]}-arm64-apple-darwin-smoke-test.zip',destination)


def sdk_evidence():
    value=validator.decode((ROOT/'build/darwin-artifact-input-evidence.json').read_bytes())
    if value['run']!=os.environ['CPKT_OPERATION_RUN']:raise ValueError('stale Darwin artifact input proof')
    handoff=value['handoff'];base=Path(value['base'])
    for name,digest in value['archives'].items():
        if sha(base/name)!=digest:raise ValueError('artifact verification changed producer archive')
    results=read(ROOT/'build/verification/arm64-apple-darwin/all/packages/proof.json')
    if results['run']!=value['run']:raise ValueError('stale native artifact composition proof')
    write_json(ROOT/'build/darwin-artifact-evidence.json',{'schema_version':1,'status':'passed','kind':'native-producer-artifact','producer_commit':handoff['producer_commit'],'tag':handoff['tag'],'manifest_sha256':handoff['manifest_sha256'],'archives':value['archives'],'draft_id':handoff['draft_id'],'jobs':int(os.environ.get('CPKT_DEPENDENCY_BUILD_JOBS',os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL','2'))),'combinations':[r['order'] for r in results['combinations']]})


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('action',choices=['source-evidence','sdk-input','sdk-smoke','sdk-evidence','smoke-zip']);parser.add_argument('--version')
    args=parser.parse_args()
    if 'CPKT_OPERATION_FD' not in os.environ:locked_run(ROOT,'all')
    delegated(ROOT,'all')
    outputs={'source-evidence':'darwin-source-evidence.json',
             'sdk-input':'darwin-artifact-input-evidence.json',
             'sdk-evidence':'darwin-artifact-evidence.json'}
    if args.action in outputs:
        path=safe_owned(ROOT/'build'/outputs[args.action])
        safe_owned(path.with_name(path.name+'.tmp'))
    if args.action=='smoke-zip':
        workspace=ROOT/'build/package-stage/arm64-apple-darwin/all/smoke'
        for path in (workspace,workspace/'darwin-smoke-test',workspace/'sdk'):
            safe_owned(path)
    if args.action=='sdk-smoke':safe_owned(ROOT/'build/darwin-artifact-smoke')
    if sys.platform=='darwin':
        from cpkt_darwin_tools import discover
        os.environ['SDKROOT']=discover(command)['CMAKE_OSX_SYSROOT']
    if args.action=='source-evidence':source_evidence()
    elif args.action=='sdk-input':sdk_input()
    elif args.action=='sdk-smoke':sdk_smoke()
    elif args.action=='sdk-evidence':sdk_evidence()
    else:smoke_zip(args.version or version())


if __name__=='__main__':
    try:sys.exit(main())
    except (ValueError,OSError,RuntimeError,KeyError) as error:sys.exit('Darwin proof: '+str(error))
