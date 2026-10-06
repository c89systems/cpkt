#!/usr/bin/env python3
"""Publish source proof only after independently reconstructed successful cases."""
import json
import os
from pathlib import Path
import sys
import xml.etree.ElementTree as ET
from cpkt_packages import ROOT, sha, write_json, invalidate_release, validator, safe_owned
from cpkt_inventory import REPOSITORY_GROUP
from cpkt_receipts import read, readiness_path, group_outputs, verification_inputs, cache
from cpkt_lock import delegated


def reconstruction_evidence(source,ver):
    target='x86_64-linux-gnu';coverage={}
    owner=json.loads((source/'build/control/operation.lock').read_text())
    if owner.get('root')!=str(source.resolve()) or owner.get('scope')!='all' or owner.get('status') not in ('running','passed'):
        raise ValueError('source evidence requires its recorded successful operation')
    runs=set()
    for group in (REPOSITORY_GROUP,):
        graph=source/'build'/target/group/'Release'
        receipt=read(readiness_path(source,target,group,'Release'))
        package=read(source/'build/verification'/target/group/'package-ready.json')
        configured=cache(graph/'CMakeCache.txt')
        if receipt.get('status')!='passed' or receipt.get('profile')!='native-linux' or not receipt.get('coverage') or (not configured.get('CPKT_DEPENDENCY_BUILD_JOBS','').isdecimal() or not 1<=int(configured['CPKT_DEPENDENCY_BUILD_JOBS'])<=8):
            raise ValueError('source reconstruction missing actual native group tests with a positive configured job limit')
        actual=group_outputs(graph)
        identity=verification_inputs(source,group,configured)
        if receipt['outputs']!=actual or receipt['verification_id']!=identity or package.get('graph_outputs')!=actual or package.get('development_id')!=identity:
            raise ValueError('source reconstruction compiled/input closure changed')
        junit=ET.parse(graph/'cpkt-test-results.xml').getroot()
        cases=list(junit.iter('testcase'))
        if sorted(c.attrib['name'] for c in cases)!=sorted(receipt['coverage']) or any(c.find(k) is not None for c in cases for k in ('skipped','failure','error')):
            raise ValueError('source reconstruction has incomplete/failed/skipped actual test results')
        log=source/'build/package-stage'/target/group/'verify/consumer.log'
        consumers=json.loads(log.read_text().splitlines()[-1])
        if package.get('status')!='passed' or not consumers or consumers!=package.get('consumer_cases') or any(c['status']!='passed' or c['runtime']!='native' for c in consumers) or sha(log)!=package.get('consumer_log_sha256'):
            raise ValueError('source reconstruction missing successful native installed consumer cases')
        delivered=source/'build/package-stage'/target/group/'archives'/('cpkt-'+ver+'-'+target+'.tar.gz')
        if sha(delivered)!=package['archive_sha256'] or package['release_version']!=ver:
            raise ValueError('source reconstruction package archive changed')
        prefix=source/'build/package-stage'/target/group/'verify'/('cpkt-'+ver+'-'+target)
        validator.validate(prefix,['core',group] if group!='core' else ['core'],ver,target)
        if validator.load_manifest(prefix,group)!=package['manifest']:raise ValueError('source reconstruction package manifest changed')
        runs.update((receipt['run'],package['run']))
        coverage[group]={'tests':receipt['coverage'],'outputs':actual,'development_id':identity,'components':receipt['components'],
                         'archive_sha256':package['archive_sha256'],'package_id':package['manifest']['package_id'],
                         'consumer_cases':consumers,'consumer_log_sha256':sha(log)}
    composition=read(source/'build/verification'/target/'all/packages/proof.json')
    runs.add(composition['run'])
    orders=[['core']] if REPOSITORY_GROUP=='core' else [['core',REPOSITORY_GROUP],[REPOSITORY_GROUP,'core']]
    if runs!={owner['run']} or composition.get('status')!='passed' or composition.get('kind')!='installed-composition' or composition.get('target_id')!=target or composition.get('release_version')!=ver or [c['order'] for c in composition['combinations']]!=orders:
        raise ValueError('source reconstruction lacks same-run complete installed composition')
    for group in coverage:
        if composition['archives'][group]!=coverage[group]['archive_sha256']:raise ValueError('source composition borrowed different archive bytes')
    from cpkt_packages import consumer_context,validate_consumer_evidence
    if set(composition.get('owned_suites',{}))!={REPOSITORY_GROUP}:raise ValueError('source composition lacks owned suites')
    for group,evidence in composition['owned_suites'].items():
        closure={group:coverage[group]['archive_sha256']}
        if group!='core':
            from cpkt_core import cached_archive
            closure['core']=sha(cached_archive(source,target))
        graph=source/'build'/target/group/'Release'
        # Contract stores the effective consumer configuration; verify current
        # code/tools/runtime/closure and actual output/log evidence independently.
        configured=evidence['context']['settings']
        if configured.get('CPKT_DEPENDENCY_BUILD_JOBS')!=cache(graph/'CMakeCache.txt')['CPKT_DEPENDENCY_BUILD_JOBS']:raise ValueError('source consumer configured jobs changed')
        context=consumer_context(source,target,'x86_64-linux-gnu-release',group,closure,configured)
        validate_consumer_evidence(source,evidence,context,owner['run'])
        if evidence['consumer_cases']!=coverage[group]['consumer_cases']:raise ValueError('source borrowed different owned consumer cases')
    for index,item in enumerate(composition['combinations']):
        if item.get('reused_owned_suites')!={REPOSITORY_GROUP:composition['owned_suites'][REPOSITORY_GROUP]['context_id']}:raise ValueError('source composition reuse accounting changed')
        log=source/'build/verification'/target/'all/packages'/str(index)/'consumer.log'
        if sha(log)!=item['consumer_log_sha256'] or json.loads(log.read_text().splitlines()[-1])!=item['consumer_cases'] or not item['consumer_cases'] or any(c['status']!='passed' or c['runtime']!='native' for c in item['consumer_cases']):
            raise ValueError('source composition actual consumer case/log evidence changed')
    return coverage,composition


def main():
    delegated(ROOT,'all')
    if sys.argv[1]=='--invalidate':
        path=safe_owned(ROOT/'build/verification/source'/sys.argv[2]/'proof.json')
        invalidate_release(sys.argv[2]);path.unlink(missing_ok=True);return
    archive,ver,source=Path(sys.argv[1]),sys.argv[2],Path(sys.argv[3]).resolve()
    if source==ROOT or not source.is_relative_to(ROOT/'build') or not (source/'VERSION').is_file() or (source/'VERSION').read_text().strip()!=ver:
        raise ValueError('source proof requires the independent extracted source/version under build/')
    owner=json.loads((source/'build/control/operation.lock').read_text())
    if owner.get('status')!='passed' or not any(Path(arg).name=='source-reconstruct.sh' for arg in owner.get('command',[])):
        raise ValueError('source proof requires the completed independent reconstruction operation')
    path=safe_owned(ROOT/'build/verification/source'/ver/'proof.json')
    safe_owned(path.with_name(path.name+'.tmp'))
    coverage,composition=reconstruction_evidence(source,ver)
    write_json(path,{'schema_version':1,'status':'passed','kind':'source-reconstruction',
        'archive_sha256':sha(archive),'release_version':ver,'run':os.environ['CPKT_OPERATION_RUN'],'coverage':coverage,'composition':composition})

if __name__=='__main__':
    try:main()
    except (ValueError,OSError,KeyError,ET.ParseError) as error:sys.exit('source proof: '+str(error))
