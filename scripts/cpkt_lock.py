#!/usr/bin/env python3
"""Portable inspection of inherited POSIX lock descriptors; no command dispatch.

Bash owns locking, process groups, sequencing and cleanup. This focused helper
checks descriptor identity, read-only scope and a live flock across Linux/macOS,
which Bash and CMake cannot inspect portably. It never builds or schedules work.
"""
import argparse
from array import array
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import secrets
import socket


def operation_fds():
    if not os.environ.get('CPKT_OPERATION_FD') or not os.environ.get('CPKT_OPERATION_CAP_FD'):
        raise RuntimeError('no inherited repository operation lock')
    return int(os.environ['CPKT_OPERATION_FD']), int(os.environ['CPKT_OPERATION_CAP_FD'])


def ensure_operation(root, group):
    """A standalone validator enters the Bash-owned operation before inspection."""
    if 'CPKT_OPERATION_FD' not in os.environ:
        os.execvp('bash', ['bash', str(Path(root)/'scripts/operation.sh'),
                          '--root', str(root), '--group', group, '--', sys.executable, *sys.argv])
    delegated(root, group)


def _read(fd):
    size = os.fstat(fd).st_size
    if not 0 < size <= 1048576:
        raise RuntimeError('invalid operation descriptor size')
    return os.pread(fd, size, 0)


def _scope(fd):
    if fcntl.fcntl(fd, fcntl.F_GETFL) & os.O_ACCMODE != os.O_RDONLY:
        raise RuntimeError('operation scope descriptor must be read-only')
    info = os.fstat(fd)
    if info.st_nlink != 0:
        raise RuntimeError('operation scope must be an unlinked inherited descriptor')
    payload = _read(fd)
    fields = payload.decode().split('\0')
    if len(fields) != 4 or fields[-1] != '':
        raise RuntimeError('invalid operation scope')
    return fields[:3], {'device': info.st_dev, 'inode': info.st_ino,
                        'sha256': hashlib.sha256(payload).hexdigest()}


def _lock(root, fd):
    root = Path(root).resolve()
    path = root/'build/control/operation.lock'
    if path.is_symlink():
        raise RuntimeError('operation lock must not be a symlink')
    actual, expected = os.fstat(fd), path.stat()
    if (actual.st_dev, actual.st_ino) != (expected.st_dev, expected.st_ino):
        raise RuntimeError('inherited operation descriptor belongs to another repository')
    probe = os.open(path, os.O_RDWR)
    try:
        try:
            fcntl.flock(probe, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise RuntimeError('descriptor is not the owning lock description') from error
            return root
        raise RuntimeError('operation descriptor does not identify a live lock')
    finally:
        os.close(probe)


def _validate(root, group):
    fd, cap = operation_fds()
    root = _lock(root, fd)
    record = json.loads(_read(fd))
    fields, identity = _scope(cap)
    owner = json.loads((root/'cmake/components.json').read_text())['repository_group']
    declared=os.environ['CPKT_OPERATION_SCOPE']
    if fields != [str(root), declared, os.environ['CPKT_OPERATION_RUN']]:
        raise RuntimeError('scope string does not match inherited scope capability')
    if group not in ('all', owner) or declared not in ('all', owner) or (declared != 'all' and group != declared):
        raise RuntimeError('operation scope is not owned by this repository')
    if (record.get('schema_version') != 1 or record.get('root') != str(root)
            or record.get('run') != fields[2] or os.environ.get('CPKT_OPERATION_ROOT') != str(root) or identity not in record.get('capabilities', [])):
        raise RuntimeError('scope descriptor was not issued by the operation owner')
    return fd, record


@contextmanager
def _socket_directory(root):
    previous = os.open('.', os.O_RDONLY)
    try:
        os.chdir(Path(root)/'build/control')
        yield '.operation.sock'
    finally:
        os.fchdir(previous)
        os.close(previous)


def _recover(root, group):
    # CMake/CTest close non-stdio descriptors. Recover only the live owner's
    # existing lock/scope handles; never reopen the lock as a new owner.
    with socket.socket(socket.AF_UNIX) as client:
        client.settimeout(2)
        with _socket_directory(root) as name:
            client.connect(name)
        request = {'token': os.environ['CPKT_OPERATION_BROKER_TOKEN'], 'group': group, 'scope': os.environ['CPKT_OPERATION_SCOPE']}
        client.sendall(json.dumps(request).encode()+b'\n')
        message, ancillary, _, _ = client.recvmsg(1024, socket.CMSG_SPACE(2*array('i').itemsize))
    descriptors = array('i')
    for level, kind, payload in ancillary:
        if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
            descriptors.frombytes(payload[:len(payload)-len(payload)%descriptors.itemsize])
    if message != b'ok' or len(descriptors) != 2:
        for descriptor in descriptors:
            os.close(descriptor)
        raise RuntimeError('operation descriptor recovery was rejected')
    for descriptor in descriptors:
        os.set_inheritable(descriptor, True)
    os.environ['CPKT_OPERATION_FD'], os.environ['CPKT_OPERATION_CAP_FD'] = map(str, descriptors)


def delegated(root, group):
    try:
        return _validate(root, group)
    except OSError as error:
        import errno
        if error.errno != errno.EBADF:
            raise
        if not os.environ.get('CPKT_OPERATION_BROKER_TOKEN'):
            raise
        _recover(root, group)
        return _validate(root, group)


def _serve(root, group):
    delegated(root, group)
    owner = os.getppid()
    path = Path(root)/'build/control/.operation.sock'
    path.unlink(missing_ok=True)
    with socket.socket(socket.AF_UNIX) as server:
        with _socket_directory(root) as name:
            server.bind(name)
        path.chmod(0o600)
        server.listen(16)
        server.settimeout(0.5)
        while os.getppid() == owner:
            try:
                client, _ = server.accept()
            except socket.timeout:
                continue
            with client:
                client.settimeout(0.5)
                try:
                    payload = b''
                    while b'\n' not in payload and len(payload) <= 4096:
                        chunk = client.recv(4096)
                        if not chunk:
                            break
                        payload += chunk
                    request = json.loads(payload.split(b'\n', 1)[0])
                    if not isinstance(request.get('token'), str):
                        raise ValueError('invalid operation token')
                    fd, record = _validate(root, request['group'])
                    scope = record['recovery_tokens'].get(hashlib.sha256(request['token'].encode()).hexdigest())
                    if scope != request['scope'] or (scope != 'all' and request['group'] != scope):
                        raise RuntimeError('recovery token cannot widen its scope')
                    cap = None
                    if scope != os.environ['CPKT_OPERATION_SCOPE']:
                        cap = _issue_scope(root, scope, fd, record)
                    try:
                        rights = array('i', (fd, cap if cap is not None else operation_fds()[1]))
                        client.sendmsg([b'ok'], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, rights)])
                    finally:
                        if cap is not None: os.close(cap)
                except (ValueError, RuntimeError, OSError, KeyError, TypeError):
                    try:
                        client.sendall(b'rejected')
                    except OSError:
                        pass
    path.unlink(missing_ok=True)


def _write(fd,record):
    payload=json.dumps(record,sort_keys=True).encode()
    os.pwrite(fd,payload,0)
    os.ftruncate(fd,len(payload))
    os.fsync(fd)


def _issue_scope(root,group,fd,record):
    temporary,name=tempfile.mkstemp(prefix='.scope-',dir=Path(root)/'build/control')
    try:
        os.write(temporary,('\0'.join((str(Path(root).resolve()),group,record['run'],''))).encode())
        cap=os.open(name,os.O_RDONLY)
    finally:
        os.close(temporary);os.unlink(name)
    os.set_inheritable(cap,True)
    _,identity=_scope(cap)
    record['capabilities'].append(identity);_write(fd,record)
    return cap


def _narrow(root,group):
    fd,record=delegated(root,'all')
    if os.environ['CPKT_OPERATION_SCOPE']!='all':
        raise RuntimeError('cannot widen a narrowed scope')
    cap=_issue_scope(root,group,fd,record)
    token=secrets.token_hex(32)
    record['recovery_tokens'][hashlib.sha256(token.encode()).hexdigest()]=group
    _write(fd,record)
    return cap,token


@contextmanager
def child_delegation(root, group):
    delegated(root,group)
    environment=dict(os.environ,GROUP=group)
    cap=None
    if environment['CPKT_OPERATION_SCOPE']!=group:
        cap,token=_narrow(root,group)
        environment.update(CPKT_OPERATION_SCOPE=group,CPKT_OPERATION_CAP_FD=str(cap),CPKT_OPERATION_BROKER_TOKEN=token)
    try:
        yield environment,(int(environment['CPKT_OPERATION_FD']),int(environment['CPKT_OPERATION_CAP_FD']))
    finally:
        if cap is not None:os.close(cap)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--group', required=True)
    parser.add_argument('--init', action='store_true')
    parser.add_argument('--broker', action='store_true')
    parser.add_argument('--narrow', action='store_true')
    parser.add_argument('--exit-status', type=int)
    parser.add_argument('--command', nargs=argparse.REMAINDER, default=[])
    argv = sys.argv[1:]
    command_index = argv.index('--command') if '--command' in argv else len(argv)
    command = argv[command_index+1:]
    args = parser.parse_args(argv[:command_index])
    args.command = command
    if args.narrow:
        cap,token=_narrow(args.root,args.group)
        os.environ.update(CPKT_OPERATION_SCOPE=args.group,CPKT_OPERATION_CAP_FD=str(cap),CPKT_OPERATION_BROKER_TOKEN=token)
        # POSIX descriptor transfer requires exec to retain the issued handle.
        # This executes only the supplied continuation, never a lifecycle action.
        continuation=args.command[1:] if args.command[:1]==['--'] else args.command
        os.execvp(continuation[0],continuation)
    if args.broker:
        _serve(args.root, args.group)
        return
    if args.exit_status is not None:
        fd, record = delegated(args.root, args.group)
        record.update(status='passed' if args.exit_status == 0 else 'failed', exit_status=args.exit_status)
        payload = json.dumps(record, sort_keys=True).encode()
        os.pwrite(fd, payload, 0)
        os.ftruncate(fd, len(payload))
        os.fsync(fd)
        return
    if not args.init:
        delegated(args.root, args.group)
        return
    fd, cap = operation_fds()
    root = _lock(args.root, fd)
    fields, identity = _scope(cap)
    if fields[0] != str(root) or fields[2] != os.environ['CPKT_OPERATION_RUN']:
        raise RuntimeError('invalid initial scope descriptor')
    record = {'schema_version': 1, 'root': str(root), 'scope': fields[1],
              'run': fields[2], 'pid': os.getppid(), 'capabilities': [identity],
              'command': args.command, 'status': 'running',
              'recovery_tokens': {hashlib.sha256(os.environ['CPKT_OPERATION_BROKER_TOKEN'].encode()).hexdigest(): fields[1]}}
    payload = json.dumps(record, sort_keys=True).encode()
    os.pwrite(fd, payload, 0)
    os.ftruncate(fd, len(payload))
    os.fsync(fd)
    delegated(root, args.group)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        sys.exit('operation delegation: '+str(error))
