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

from cpkt_packages import ROOT, command, version, stage, verify_selected, selected_archive, combinations, archive_name, prefix_name, safe_extract, validator, write_json, sha, invalidate_release, privacy
from cpkt_operation import delegated, run as locked_run
from cpkt_inventory import REPOSITORY_GROUP
from cpkt_receipts import read


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
    workspace=ROOT/'build/package-stage'/target/'all/smoke';workspace.mkdir(parents=True,exist_ok=True)
    package=workspace/'darwin-smoke-test'
    if package.exists():shutil.rmtree(package)
    (package/'bin').mkdir(parents=True);(package/'lib').mkdir()
    extraction=workspace/'sdk'
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
    destination=base/f'cpkt-{ver}-{target}-smoke-test.zip'
    invalidate_release(ver)
    write_smoke_archive(package,destination)
    privacy([destination])
    return destination


def native_source():
    if sys.platform!='darwin' or os.uname().machine!='arm64':raise ValueError('native source proof requires arm64 Darwin')
    os.environ['CPKT_DEPENDENCY_BUILD_JOBS']='2'
    for variable,tool in [('CPKT_DARWIN_HOST_MIG','mig'),('CPKT_DARWIN_HOST_MIGCOM','migcom'),('CPKT_OTOOL','otool'),('CMAKE_OTOOL','otool')]:
        os.environ[variable]=command(['xcrun','--find',tool],capture=True).strip()
    preset='arm64-apple-darwin-native';ver=version();target='arm64-apple-darwin'
    command([sys.executable,ROOT/'scripts/group-build.py','preflight','--group','all','--preset',preset])
    for group in (REPOSITORY_GROUP,):
        command([sys.executable,ROOT/'scripts/group-build.py','test','--group',group,'--preset',preset],group=group)
        archive,_=stage(preset,group,ver);verify_selected(preset,group,ver)
        invalidate_release(ver);(ROOT/'dist').mkdir(exist_ok=True);shutil.copy2(archive,ROOT/'dist'/archive.name)
    # Installed composition is verified below against delivered archive bytes.
    results=combinations(preset,ver,ROOT/'dist')
    smoke_zip(ver)
    commit=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
    write_json(ROOT/'build/darwin-source-evidence.json',{'schema_version':1,'status':'passed','kind':'native-source','commit':commit,'platform':os.uname().release,'compiler':command(['xcrun','clang','--version'],capture=True),'sdk':command(['xcrun','--show-sdk-version'],capture=True).strip(),'deployment_floor':'15.0','jobs':2,'coverage':[read(ROOT/'build/verification'/target/g/'Release-development.json')['coverage'] for g in (REPOSITORY_GROUP,)],'combinations':[r['order'] for r in results]})


def native_sdk():
    if sys.platform!='darwin' or os.uname().machine!='arm64':raise ValueError('artifact runtime proof requires actual native arm64 Darwin')
    base=Path(os.environ.get('CPKT_DARWIN_ARTIFACT_DIR',str(ROOT/'build/darwin-artifact-input')))
    from cpkt_github_handoff import validate_handoff
    handoff=validate_handoff(validator.decode((base/'handoff.json').read_bytes()))
    commit=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
    if commit!=handoff['producer_commit'] or os.environ.get('CPKT_EXPECTED_PRODUCER_COMMIT',commit)!=commit:raise ValueError('artifact lane checked-out producer commit mismatch')
    for name,item in handoff['assets'].items():
        if '-arm64-apple-darwin' in name or name.endswith('-CHECKSUMS'):
            if sha(base/name)!=item['sha256'] or (base/name).stat().st_size!=item['size']:raise ValueError('artifact input bytes changed: '+name)
    before={name:sha(base/name) for name in handoff['assets'] if '-arm64-apple-darwin' in name}
    results=combinations('arm64-apple-darwin-native',handoff['version'],base,reuse_owned=False)
    # Actual original smoke executables are exercised without any SDK mutation.
    zip_path=base/f'cpkt-{handoff["version"]}-arm64-apple-darwin-smoke-test.zip'
    destination=ROOT/'build/darwin-artifact-smoke'
    if destination.exists():shutil.rmtree(destination)
    destination.mkdir()
    extract_smoke(zip_path,destination)
    for name in ('cpkt_abi_smoke_static','cpkt_abi_smoke_shared'):
        executable=destination/'darwin-smoke-test/bin'/name
        command(['codesign','--verify','--strict',executable])
        command([executable])
    for name,digest in before.items():
        if sha(base/name)!=digest:raise ValueError('artifact verification changed producer archive')
    write_json(ROOT/'build/darwin-artifact-evidence.json',{'schema_version':1,'status':'passed','kind':'native-producer-artifact','producer_commit':commit,'tag':handoff['tag'],'manifest_sha256':handoff['manifest_sha256'],'archives':before,'draft_id':handoff['draft_id'],'jobs':2,'combinations':[r['order'] for r in results]})


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('action',choices=['source','sdk','smoke-zip']);parser.add_argument('--version')
    args=parser.parse_args()
    if 'CPKT_OPERATION_FD' not in os.environ:return locked_run(ROOT,'all',[sys.executable,__file__]+sys.argv[1:])
    delegated(ROOT,'all')
    if args.action=='source':native_source()
    elif args.action=='sdk':native_sdk()
    else:smoke_zip(args.version or version())


if __name__=='__main__':
    try:sys.exit(main())
    except (ValueError,OSError,RuntimeError,KeyError) as error:sys.exit('Darwin proof: '+str(error))
