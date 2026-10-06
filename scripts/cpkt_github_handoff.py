#!/usr/bin/env python3
"""Authenticated, digest-pinned unpublished draft transport; no automatic publish."""
import argparse
import hashlib
import fcntl
import stat
import tempfile
import time
import http.client
import json
import os
from pathlib import Path
import re
import shutil
import sys
import subprocess
import urllib.error
import urllib.parse
import urllib.request

from cpkt_packages import ROOT, artifacts, canonical, sha, write_json, check_snapshot, safe_owned
from cpkt_lock import delegated, ensure_operation as locked_run
from cpkt_receipts import read

REPOSITORY='c89systems/cpkt'
API='https://api.github.com'


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs):return None


def git(root,*arguments):
    result=subprocess.run(['git','-C',str(root),*arguments],capture_output=True,text=True,check=True)
    return result.stdout.strip()


class GitHub:
    def __init__(self,token=None,opener=None):
        self.token=token or os.environ.get('CPKT_DRAFT_TOKEN') or os.environ.get('GH_TOKEN')
        if not isinstance(self.token,str) or not re.fullmatch(r'[!-~]+',self.token):raise ValueError('authenticated draft-read token is required (CPKT_DRAFT_TOKEN or GH_TOKEN, repository Contents read; write for staging)')
        self.opener=opener or urllib.request.build_opener(NoRedirect())

    def open(self,url,accept='application/vnd.github+json',method='GET',data=None,authenticated=True,redirects=0):
        parsed=urllib.parse.urlsplit(url)
        if parsed.scheme!='https' or parsed.username or parsed.password or parsed.port not in (None,443):raise ValueError('unsafe transport URL')
        if authenticated and parsed.hostname not in ('api.github.com','uploads.github.com'):raise ValueError('credentials may only reach official GitHub API hosts')
        if not authenticated and parsed.hostname not in ('release-assets.githubusercontent.com','objects.githubusercontent.com'):raise ValueError('untrusted asset redirect host')
        headers={'Accept':accept,'X-GitHub-Api-Version':'2026-03-10','User-Agent':'cpkt-draft-handoff'}
        if data is not None:headers['Content-Type']='application/json'
        if authenticated:headers['Authorization']='Bearer '+self.token
        request=urllib.request.Request(url,data=data,headers=headers,method=method)
        try:return self.opener.open(request,timeout=120)
        except urllib.error.HTTPError as error:
            if error.code in (301,302,303,307,308) and method=='GET' and accept=='application/octet-stream':
                if redirects >= 2:raise ValueError('asset redirect limit exceeded')
                redirect=urllib.parse.urljoin(url,error.headers.get('Location',''))
                error.close()
                return self.open(redirect,accept,authenticated=False,redirects=redirects+1)
            # URLs/headers/token/signed redirect queries never enter diagnostics.
            raise ValueError('GitHub authenticated request failed with HTTP '+str(error.code)) from None
        except (urllib.error.URLError,TimeoutError):raise ValueError('GitHub authenticated transport failed') from None

    def json(self,path,method='GET',data=None):
        payload=canonical(data) if data is not None else None
        with self.open(API+path,method=method,data=payload) as response:
            return json.load(response)

    def upload(self,url,path):
        parsed=urllib.parse.urlsplit(url)
        if parsed.scheme!='https' or parsed.hostname!='uploads.github.com' or parsed.port not in (None,443) or parsed.username or parsed.password:raise ValueError('unexpected official upload endpoint')
        connection=http.client.HTTPSConnection(parsed.hostname,timeout=120)
        try:
            connection.putrequest('POST',parsed.path+'?'+urllib.parse.urlencode({'name':path.name}))
            connection.putheader('Authorization','Bearer '+self.token)
            connection.putheader('Accept','application/vnd.github+json')
            connection.putheader('X-GitHub-Api-Version','2026-03-10')
            connection.putheader('Content-Type','application/octet-stream')
            connection.putheader('Content-Length',str(path.stat().st_size))
            connection.endheaders()
            with path.open('rb') as source:
                for chunk in iter(lambda:source.read(1048576),b''):connection.send(chunk)
            response=connection.getresponse()
            if response.status!=201:raise ValueError('authenticated upload failed with HTTP '+str(response.status))
            return json.load(response)
        finally:connection.close()


def validate_handoff(item):
    if not isinstance(item,dict) or set(item)!={'schema_version','repository','producer_commit','tag','version','manifest_sha256','draft_id','assets'} or type(item['schema_version']) is not int or item['schema_version']!=1 or item['repository']!=REPOSITORY:raise ValueError('unknown handoff schema/fields/repository')
    if not re.fullmatch('[0-9a-f]{40}|[0-9a-f]{64}',item['producer_commit']) or item['tag']!='v'+item['version'] or item['version']=='0.0.0':raise ValueError('artifact mode requires actual final lightweight tag/producer identity')
    if type(item['draft_id']) is not int or item['draft_id']<=0:raise ValueError('invalid draft identity')
    expected=set(artifacts(item['version'],'release'))|{f'cpkt-{item["version"]}-CHECKSUMS'}
    if set(item['assets'])!=expected or len(item['assets'])!=len(expected):raise ValueError('handoff must enumerate the complete declared upload assets')
    ids=set()
    for name,asset in item['assets'].items():
        if set(asset)!={'id','size','sha256'} or type(asset['id']) is not int or asset['id']<=0 or asset['id'] in ids or type(asset['size']) is not int or asset['size']<=0:raise ValueError('invalid/duplicate asset identity')
        if not re.fullmatch('[0-9a-f]{64}',asset['sha256']):raise ValueError('invalid asset digest')
        ids.add(asset['id'])
    if item['assets'][f'cpkt-{item["version"]}-CHECKSUMS']['sha256']!=item['manifest_sha256']:raise ValueError('manifest digest mismatch')
    return item


def draft_matches(api,handoff):
    validate_handoff(handoff)
    base='/repos/'+REPOSITORY
    tag=api.json(base+'/git/ref/tags/'+urllib.parse.quote(handoff['tag'],safe=''))
    if tag['object'].get('type')!='commit' or tag['object'].get('sha')!=handoff['producer_commit']:raise ValueError('remote lightweight tag/producer commit mismatch')
    draft=api.json(base+'/releases/'+str(handoff['draft_id']))
    if draft.get('draft') is not True or draft.get('tag_name')!=handoff['tag'] or draft.get('id')!=handoff['draft_id']:raise ValueError('authenticated draft-read identity mismatch')
    assets=api.json(base+'/releases/'+str(handoff['draft_id'])+'/assets?per_page=100')
    if len(assets)!=len(handoff['assets']) or {x['name'] for x in assets}!=set(handoff['assets']):raise ValueError('server draft asset inventory mismatch')
    for asset in assets:
        expected=handoff['assets'][asset['name']]
        if asset.get('id')!=expected['id'] or asset.get('size')!=expected['size'] or asset.get('state')!='uploaded' or asset.get('digest')!='sha256:'+expected['sha256']:raise ValueError('server asset size/digest/ID mismatch: '+asset['name'])
    return draft


def cache_path(path):
    """Declared shared archive state may be outside the repository."""
    path=Path(os.path.abspath(path))
    for parent in (path,*path.parents):
        if parent.is_symlink():raise ValueError('shared archive cache has a symlink ancestor')
    return path


def verified_cache_hit(cache_root,asset):
    root=cache_path(cache_root)
    if not root.exists():return None
    if not root.is_dir():raise ValueError('shared archive cache is not a directory')
    # Names, source URL and extraction root are not archive identities.
    for directory,dirs,files in os.walk(root,followlinks=False):
        # Closing any descriptor for a lock inode would release this process's
        # POSIX lock; lock metadata and unpublished parts are never archives.
        dirs[:]=[d for d in dirs if not (Path(directory)/d).is_symlink() and not (Path(directory)==root and d=='locks')]
        for name in sorted(files):
            if name.startswith('.') and ('.part-' in name or name.startswith('.part-')):continue
            path=Path(directory)/name
            if path.is_symlink():continue
            try:
                fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
                with os.fdopen(fd,'rb') as stream:
                    info=os.fstat(stream.fileno())
                    if not stat.S_ISREG(info.st_mode) or info.st_size!=asset['size']:continue
                    digest=hashlib.sha256()
                    for chunk in iter(lambda:stream.read(1048576),b''):digest.update(chunk)
                    if digest.hexdigest()==asset['sha256']:return path
            except FileNotFoundError:continue
    return None


def acquire(api,asset,cache_root,pre_acquire=None):
    digest=asset['sha256']
    if not isinstance(digest,str) or not re.fullmatch('[0-9a-f]{64}',digest) or type(asset['size']) is not int or asset['size']<=0:raise ValueError('invalid pinned asset digest/size')
    root=cache_path(cache_root)
    hit=verified_cache_hit(root,asset)
    if hit:return hit
    root.mkdir(parents=True,exist_ok=True)
    cache_path(root)
    locks=cache_path(root/'locks');locks.mkdir(exist_ok=True)
    lock_path=cache_path(locks/(digest+'.lock'))
    fd=os.open(lock_path,os.O_CREAT|os.O_RDWR|os.O_NOFOLLOW,0o600)
    temporary=None
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):raise ValueError('invalid digest lock')
        # Match CMake file(LOCK): POSIX byte-range locks, not BSD flock.
        deadline=time.monotonic()+120
        while True:
            try:fcntl.lockf(fd,fcntl.LOCK_EX|fcntl.LOCK_NB);break
            except BlockingIOError:
                if time.monotonic()>=deadline:raise ValueError('shared archive digest lock timed out')
                time.sleep(0.05)
        # Another process may have published renamed bytes while we waited.
        hit=verified_cache_hit(root,asset)
        if hit:return hit
        archive_dir=cache_path(root/'archives/sha256'/digest)
        archive_dir.mkdir(parents=True,exist_ok=True)
        destination=cache_path(archive_dir/('sha256-'+digest))
        if destination.exists() and not destination.is_file():raise ValueError('invalid shared archive destination')
        if pre_acquire:pre_acquire()
        part_fd,part=tempfile.mkstemp(prefix='.part-'+digest+'-',dir=archive_dir)
        temporary=Path(part)
        with os.fdopen(part_fd,'wb') as output,api.open(API+'/repos/'+REPOSITORY+'/releases/assets/'+str(asset['id']),accept='application/octet-stream') as source:
            for chunk in iter(lambda:source.read(1048576),b''):output.write(chunk)
            output.flush();os.fsync(output.fileno())
        if temporary.stat().st_size!=asset['size'] or sha(temporary)!=digest:raise ValueError('downloaded pinned asset size/digest mismatch')
        cache_path(destination)
        temporary.replace(destination)
        return destination
    finally:
        if temporary:temporary.unlink(missing_ok=True)
        os.close(fd)


def acquisition_plan(handoff,shared_cache):
    result={};missing=[]
    for name,asset in handoff['assets'].items():
        if not (name.endswith('-CHECKSUMS') or '-arm64-apple-darwin' in name):continue
        hit=verified_cache_hit(shared_cache,asset)
        if hit:result[name]=hit
        else:missing.append(name)
    return result,missing


def dispatch_identity(handoff,commit,environment):
    if commit!=handoff['producer_commit']:raise ValueError('checked-out producer commit mismatch')
    if environment.get('GITHUB_ACTIONS')=='true':
        if environment.get('GITHUB_REF_TYPE')!='tag' or environment.get('GITHUB_REF_NAME')!=handoff['tag'] or environment.get('GITHUB_SHA')!=commit:raise ValueError('artifact dispatch requires the exact final tag and producer SHA')


def download_handoff(api,handoff,shared_cache,destination):
    validate_handoff(handoff)
    destination=safe_owned(destination)
    for name in list(handoff['assets'])+['handoff.json','handoff.json.tmp']:
        safe_owned(destination/name)
    destination.mkdir(parents=True,exist_ok=True)
    cached,missing=acquisition_plan(handoff,shared_cache)
    authenticated=False
    def pre_acquire():
        nonlocal authenticated
        if not authenticated:draft_matches(api,handoff);authenticated=True
    for name in sorted(set(cached)|set(missing)):
        source=acquire(api,handoff['assets'][name],shared_cache,pre_acquire)
        installed=safe_owned(destination/name)
        shutil.copy2(source,installed)
        asset=handoff['assets'][name]
        if installed.stat().st_size!=asset['size'] or sha(installed)!=asset['sha256']:raise ValueError('copied pinned asset identity changed')
    manifest=destination/f'cpkt-{handoff["version"]}-CHECKSUMS'
    lines={}
    for line in manifest.read_text().splitlines():
        match=re.fullmatch(r'([0-9a-f]{64})  ([^/\\]+)',line)
        if not match or match[2] in lines:raise ValueError('invalid/duplicate downloaded checksum entry')
        lines[match[2]]=match[1]
    expected={name:asset['sha256'] for name,asset in handoff['assets'].items() if not name.endswith('-CHECKSUMS')}
    if lines!=expected:raise ValueError('downloaded complete checksum inventory differs from pinned handoff')
    write_json(destination/'handoff.json',handoff)


def stage(api,commit,tag,ver):
    path=safe_owned(ROOT/'build/verification/release'/ver/'darwin-handoff.json')
    safe_owned(path.with_name(path.name+'.tmp'))
    if tag!='v'+ver or ver=='0.0.0':raise ValueError('draft staging requires an actual final tag')
    proof=read(ROOT/'build/verification/release'/ver/'proof.json')
    manifest=ROOT/'dist'/f'cpkt-{ver}-CHECKSUMS'
    if proof.get('kind')!='artifact-release' or proof.get('manifest_sha256')!=sha(manifest):raise ValueError('full local release-scope proof is required before draft staging')
    local=check_snapshot(manifest,ROOT/'dist',ver,'release',current_run=False)
    if proof.get('artifacts')!=local:raise ValueError('full local release payload identities changed')
    if git(ROOT,'rev-parse','HEAD')!=commit or git(ROOT,'rev-parse','refs/tags/'+tag)!=commit or git(ROOT,'cat-file','-t','refs/tags/'+tag)!='commit' or git(ROOT,'status','--porcelain'):raise ValueError('draft staging requires clean exact tagged producer commit')
    ref=api.json('/repos/'+REPOSITORY+'/git/ref/tags/'+urllib.parse.quote(tag,safe=''))
    if ref['object']!={'type':'commit','sha':commit,'url':ref['object'].get('url')}:raise ValueError('existing remote final lightweight tag does not match producer')
    releases=api.json('/repos/'+REPOSITORY+'/releases?per_page=100')
    if any(x.get('tag_name')==tag for x in releases):raise ValueError('an existing release/draft for this tag requires explicit reconciliation')
    release=api.json('/repos/'+REPOSITORY+'/releases',method='POST',data={'tag_name':tag,'target_commitish':commit,'name':tag,'draft':True,'prerelease':False})
    names=sorted(local)+[manifest.name];assets={}
    for name in names:
        path=ROOT/'dist'/name;asset=api.upload(release['upload_url'].split('{',1)[0],path)
        assets[name]={'id':asset['id'],'size':path.stat().st_size,'sha256':sha(path)}
    handoff={'schema_version':1,'repository':REPOSITORY,'producer_commit':commit,'tag':tag,'version':ver,'manifest_sha256':sha(manifest),'draft_id':release['id'],'assets':assets}
    draft_matches(api,handoff)
    write_json(ROOT/'build/verification/release'/ver/'darwin-handoff.json',handoff)
    return handoff


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['preflight','stage','download','verify-draft'])
    parser.add_argument('--handoff',type=Path);parser.add_argument('--producer-commit');parser.add_argument('--tag');parser.add_argument('--version')
    parser.add_argument('--authorize-release',action='store_true',help='Explicit final release operation; never set by automatic local gates')
    args=parser.parse_args()
    if args.action=='stage' and not args.authorize_release:parser.error('draft staging needs explicit --authorize-release after full local proof')
    if 'CPKT_OPERATION_FD' not in os.environ:locked_run(ROOT,'all')
    delegated(ROOT,'all');api=GitHub()
    if args.action=='stage':stage(api,args.producer_commit,args.tag,args.version);return
    if not args.handoff:parser.error('authenticated draft read preflight/download requires --handoff')
    from cpkt_packages import validator
    handoff=validate_handoff(validator.decode(args.handoff.read_bytes()))
    if args.producer_commit and args.producer_commit!=handoff['producer_commit']:raise ValueError('expected producer commit mismatch')
    if args.action=='verify-draft':draft_matches(api,handoff);return
    dispatch_identity(handoff,git(ROOT,'rev-parse','HEAD'),os.environ)
    if args.action=='preflight':draft_matches(api,handoff);return
    # Acquisition uses shared archive cache, not compiled outputs or bearer data.
    cache_root=Path(os.environ.get('CPKT_DEPENDENCY_CACHE',str(Path(os.environ.get('XDG_CACHE_HOME',str(Path.home()/'.cache')))/'cpkt/deps')))
    shared_cache=cache_root
    download_handoff(api,handoff,shared_cache,ROOT/'build/darwin-artifact-input')


if __name__=='__main__':
    try:sys.exit(main())
    except (ValueError,RuntimeError,OSError,KeyError) as error:sys.exit('draft handoff: '+str(error))
