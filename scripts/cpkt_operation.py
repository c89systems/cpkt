#!/usr/bin/env python3
"""One stable repository lock; verified inherited descriptor delegation."""
import argparse
import errno
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid
import math
import re
import tempfile
import hashlib
import secrets
import shutil
import socket
import threading
from array import array
from contextlib import contextmanager

from cpkt_inventory import GROUPS


class OperationError(RuntimeError):
    pass


def owner_record(fd):
    size = os.fstat(fd).st_size
    if size > 1048576:
        raise OperationError('operation owner record exceeds its bound')
    return json.loads(os.pread(fd, size, 0))


def operation_fds():
    return (int(os.environ['CPKT_OPERATION_FD']), int(os.environ['CPKT_OPERATION_CAP_FD']))


def capability(fd, owner):
    cap = int(os.environ['CPKT_OPERATION_CAP_FD'])
    stat = os.fstat(cap)
    if fcntl.fcntl(cap, fcntl.F_GETFL) & os.O_ACCMODE != os.O_RDONLY:
        raise OperationError('scope capability descriptor must be read-only')
    payload = os.pread(cap, stat.st_size, 0)
    identity = {'device':stat.st_dev,'inode':stat.st_ino,'sha256':hashlib.sha256(payload).hexdigest()}
    if identity not in owner.get('capabilities', []):
        raise OperationError('scope capability was not issued by this lock owner')
    result = json.loads(payload)
    if result['root'] != owner['root'] or result['run'] != owner['run']:
        raise OperationError('scope capability root/run mismatch')
    if os.environ['CPKT_OPERATION_SCOPE'] != result['scope']:
        raise OperationError('scope string does not match inherited scope capability')
    return result


def issue_capability(root, fd, owner, scope):
    # The unlinked inode cannot be reopened through a pathname. Children receive
    # only its read-only descriptor; each narrowed child drops the wider handle.
    temporary, name = tempfile.mkstemp(prefix='.operation-scope-',dir=root/'build/control')
    payload = json.dumps({'root':str(root),'run':owner['run'],'scope':scope}).encode()
    try:
        os.write(temporary,payload)
        os.fsync(temporary)
        cap = os.open(name,os.O_RDONLY)
    finally:
        os.close(temporary)
        os.unlink(name)
    stat = os.fstat(cap)
    owner.setdefault('capabilities',[]).append({'device':stat.st_dev,'inode':stat.st_ino,'sha256':hashlib.sha256(payload).hexdigest()})
    encoded = json.dumps(owner).encode()
    os.pwrite(fd,encoded,0)
    os.ftruncate(fd,len(encoded))
    os.fsync(fd)
    return cap


def broker_path(root):
    return Path(root) / 'build/control/.operation.sock'


@contextmanager
def short_socket_path(path):
    # AF_UNIX limits the supplied name, not the resolved directory path.
    # Source reconstructions can live below a much longer temporary root.
    previous = os.open('.', os.O_RDONLY)
    try:
        os.chdir(path.parent)
        yield path.name
    finally:
        os.fchdir(previous)
        os.close(previous)


def broker_request(root, scope):
    token = os.environ.get('CPKT_OPERATION_BROKER_TOKEN', '')
    if not token:
        raise OperationError('operation broker token is absent')
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        with short_socket_path(broker_path(root)) as name:
            client.connect(name)
        client.sendall(json.dumps({'token':token,'scope':scope}).encode() + b'\n')
        response = b''
        received = array('i')
        while b'\n' not in response:
            payload, controls, _, _ = client.recvmsg(4096, socket.CMSG_SPACE(2 * received.itemsize))
            if not payload:
                raise OperationError('operation broker closed without a response')
            response += payload
            for level, kind, value in controls:
                if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
                    received.frombytes(value[:len(value) - len(value) % received.itemsize])
        answer = json.loads(response.split(b'\n',1)[0])
        if answer.get('status') != 'ok' or len(received) != 2:
            for descriptor in received:
                os.close(descriptor)
            raise OperationError('operation broker rejected delegated scope')
        return received[0], received[1], answer['token']


class OperationBroker:
    """Return the live lock and scoped capability across CMake's FD closing."""

    def __init__(self, root, fd, scopes):
        self.path = broker_path(root)
        if self.path.is_symlink():
            raise OperationError('operation broker path must not be a symlink')
        self.path.unlink(missing_ok=True)
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        with short_socket_path(self.path) as name:
            self.socket.bind(name)
        os.chmod(self.path, 0o600)
        self.socket.listen(16)
        self.socket.settimeout(0.2)
        self.fd = fd
        self.scopes = scopes
        self.tokens = {scope:secrets.token_hex(32) for scope in scopes}
        self.running = True
        self.thread = threading.Thread(target=self.serve,daemon=True)
        self.thread.start()

    def serve(self):
        while self.running:
            try:
                client, _ = self.socket.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            with client:
                client.settimeout(0.5)
                try:
                    request = b''
                    while b'\n' not in request and len(request) <= 4096:
                        chunk = client.recv(4096)
                        if not chunk:
                            break
                        request += chunk
                    data = json.loads(request.split(b'\n',1)[0])
                    source = next((scope for scope, token in self.tokens.items()
                                   if secrets.compare_digest(token,data.get('token',''))),None)
                    scope = data.get('scope')
                    if source is None or scope not in self.scopes or (source != 'all' and source != scope):
                        raise OperationError('invalid broker scope capability')
                    response = json.dumps({'status':'ok','token':self.tokens[scope]}).encode()+b'\n'
                    rights = array('i',[self.fd,self.scopes[scope]])
                    client.sendmsg([response],[(socket.SOL_SOCKET,socket.SCM_RIGHTS,rights)])
                except (OSError, ValueError, KeyError, TypeError, OperationError):
                    try:client.sendall(b'{"status":"rejected"}\n')
                    except OSError:pass

    def close(self):
        self.running = False
        self.socket.close()
        self.thread.join(timeout=1)
        self.path.unlink(missing_ok=True)


def temporary_directory(root,run):
    if not re.fullmatch(r'[A-Za-z0-9_-]+',str(run)):raise OperationError('unsafe operation temporary namespace')
    directory=Path(root)/'build/control/tmp'/str(run)
    for path in (directory,*directory.parents):
        if path==Path(root):break
        if path.is_symlink():raise OperationError('operation temporary ancestry must not be a symlink')
    directory.mkdir(parents=True,exist_ok=True)
    return str(directory)


@contextmanager
def child_delegation(root, scope):
    root = Path(root).resolve()
    fd, owner = delegated(root,scope)
    current = capability(fd,owner)
    temporary=temporary_directory(root,owner['run'])
    fresh = current['scope'] != scope
    token = os.environ.get('CPKT_OPERATION_BROKER_TOKEN','')
    if fresh and token:
        borrowed, cap, token = broker_request(root,scope)
        os.close(borrowed)
    else:
        cap = issue_capability(root,fd,owner,scope) if fresh else int(os.environ['CPKT_OPERATION_CAP_FD'])
    env = dict(os.environ,CPKT_OPERATION_SCOPE=scope,CPKT_OPERATION_CAP_FD=str(cap),
               CPKT_OPERATION_BROKER_TOKEN=token,GROUP=scope,TMPDIR=temporary)
    try:
        yield env,(fd,cap)
    finally:
        if fresh:
            os.close(cap)


def _validate_delegated(root, scope):
    root = Path(root).resolve()
    try:
        fd = int(os.environ['CPKT_OPERATION_FD'])
        stat = os.fstat(fd)
        lock = root / 'build/control/operation.lock'
        expected = lock.stat()
        if (stat.st_dev, stat.st_ino) != (expected.st_dev, expected.st_ino):
            raise OperationError('delegation descriptor does not identify this repository lock')
        # A separately opened description must be blocked, while our inherited
        # description must own the live lock. Strings or a reopened fd fail.
        probe = os.open(lock, os.O_RDWR)
        try:
            try:
                fcntl.flock(probe, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                pass
            else:
                fcntl.flock(probe, fcntl.LOCK_UN)
                raise OperationError('delegation has no live lock owner')
        finally:
            os.close(probe)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise OperationError('descriptor is not the owning lock description') from error
        owner = owner_record(fd)
        if (owner['root'] != str(root) or owner['run'] != os.environ['CPKT_OPERATION_RUN']
                or os.environ['CPKT_OPERATION_ROOT'] != str(root)):
            raise OperationError('delegation root/run mismatch')
        declared = os.environ['CPKT_OPERATION_SCOPE']
        capability(fd,owner)
        if owner['scope'] != 'all' and declared != owner['scope']:
            raise OperationError('delegation widens owner scope')
        if declared != 'all' and scope != declared:
            raise OperationError('child scope widens delegated scope: ' + scope)
        return fd, owner
    except (KeyError, ValueError, OSError) as error:
        raise OperationError('invalid inherited operation delegation: ' + str(error)) from error


def delegated(root, scope):
    try:
        return _validate_delegated(root,scope)
    except OperationError as error:
        # Recover only descriptors closed by an intermediate executor. A live
        # but different descriptor is a forged delegation, even with a token.
        cause = error.__cause__
        if (not os.environ.get('CPKT_OPERATION_BROKER_TOKEN')
                or not isinstance(cause,OSError) or cause.errno != errno.EBADF):
            raise
        fd, cap, token = broker_request(root,scope)
        os.environ.update(CPKT_OPERATION_FD=str(fd),CPKT_OPERATION_CAP_FD=str(cap),
                          CPKT_OPERATION_BROKER_TOKEN=token)
        try:
            return _validate_delegated(root,scope)
        except BaseException:
            os.close(fd)
            os.close(cap)
            raise


def run(root, scope, command, timeout=120):
    started = time.monotonic()
    root = Path(root).resolve()
    if scope not in GROUPS:
        raise OperationError('unknown GROUP: ' + scope)
    if not math.isfinite(timeout) or timeout < 0:
        raise OperationError('operation timeout must be finite and nonnegative')
    inherited = 'CPKT_OPERATION_FD' in os.environ
    if inherited:
        with child_delegation(root,scope) as (env,fds):
            return subprocess.call(command,env=env,pass_fds=fds)
    else:
        control = root / 'build/control'
        if (root / 'build').is_symlink() or control.is_symlink():
            raise OperationError('operation control ancestry must not be a symlink')
        control.mkdir(parents=True, exist_ok=True)
        lock = control / 'operation.lock'
        if lock.is_symlink():
            raise OperationError('operation lock must not be a symlink')
        fd = os.open(lock, os.O_CREAT | os.O_RDWR, 0o600)
        deadline = time.monotonic() + timeout
        while True:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError:
                if time.monotonic() >= deadline:
                    description = os.pread(fd, 8192, 0).decode(errors='replace')
                    os.close(fd)
                    raise OperationError('operation lock wait expired; owner: ' + description)
                time.sleep(min(0.1, max(0, deadline - time.monotonic())))
        owner = {'schema_version': 1, 'root': str(root), 'scope': scope,
                 'run': uuid.uuid4().hex, 'pid': os.getpid(), 'command': command,
                 'lock_wait_seconds':time.monotonic()-started,'status':'running'}
        payload = json.dumps(owner).encode()
        os.ftruncate(fd, 0)
        os.pwrite(fd, payload, 0)
        os.fsync(fd)
    broker = None
    scopes = {}
    try:
        temporary=temporary_directory(root,owner['run'])
        cap = issue_capability(root,fd,owner,scope)
        scopes[scope] = cap
        if scope == 'all':
            for child_scope in GROUPS:
                if child_scope == 'all':continue
                scopes[child_scope] = issue_capability(root,fd,owner,child_scope)
        broker = OperationBroker(root,fd,scopes)
    except BaseException:
        if broker is not None:broker.close()
        for issued in scopes.values():os.close(issued)
        os.close(fd)
        raise
    env = dict(os.environ, CPKT_OPERATION_FD=str(fd), CPKT_OPERATION_ROOT=str(root),
               CPKT_OPERATION_CAP_FD=str(cap),
               CPKT_OPERATION_BROKER_TOKEN=broker.tokens[scope],
               CPKT_OPERATION_SCOPE=scope, CPKT_OPERATION_RUN=owner['run'], GROUP=scope,
               TMPDIR=temporary)
    try:
        status = subprocess.call(command, env=env, pass_fds=(fd,cap))
        if status < 0:status = 128-status
        completed = owner_record(fd)
        completed.update(status='passed' if status == 0 else 'failed',
                         exit_status=status,seconds=time.monotonic()-started)
        payload=json.dumps(completed).encode()
        os.pwrite(fd,payload,0)
        os.ftruncate(fd,len(payload))
        os.fsync(fd)
        return status
    finally:
        if not inherited:
            # Do not explicitly unlock: surviving children still retain ownership
            # after owner interruption. Closing releases only when all holders exit.
            try:
                broker.close()
                shutil.rmtree(temporary)
                try:Path(temporary).parent.rmdir()
                except OSError:pass
            finally:
                for issued in scopes.values():os.close(issued)
                os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument('--group', default=os.environ.get('GROUP', 'all'), choices=GROUPS)
    parser.add_argument('--timeout', type=float, default=float(os.environ.get('CPKT_OPERATION_TIMEOUT', '120')))
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--source-root', help='explicit separate reconstruction repository context')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.check:
        delegated(args.root, args.group)
        return 0
    command = args.command
    if command[:1] == ['--']:
        command = command[1:]
    if not command:
        parser.error('a command is required after --')
    if args.source_root:
        # Retain the outer lock while opening an independent extracted-root lock.
        delegated(args.root, 'all')
        target = Path(args.source_root).resolve()
        if target == Path(args.root).resolve() or not (target / 'CMakeLists.txt').is_file():
            raise OperationError('source reconstruction requires a separate repository root')
        for key in list(os.environ):
            if key.startswith('CPKT_OPERATION_'):
                del os.environ[key]
        return run(target, args.group, command, args.timeout)
    return run(args.root, args.group, command, args.timeout)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, json.JSONDecodeError) as error:
        print('cpkt-operation: ' + str(error), file=sys.stderr)
        sys.exit(2)
