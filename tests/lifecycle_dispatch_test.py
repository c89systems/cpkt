"""Observe public Make dispatch with inert build leaves; never build dependencies."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
(ROOT / 'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='dispatch-') as scratch:
    root = Path(scratch)
    (root / 'scripts').mkdir()
    shutil.copy2(ROOT / 'Makefile', root / 'Makefile')
    shutil.copy2(ROOT / 'scripts/lifecycle.sh', root / 'scripts/lifecycle.sh')
    (root / 'scripts/lifecycle-common.sh').write_text('''
cpkt_scripts=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cpkt_owner=core
cpkt_fail() { echo "$*" >&2; exit 2; }
cpkt_check_group() { case "$1" in all|core) ;; *) cpkt_fail 'wrong owner';; esac; }
cpkt_default_preset() { echo debug; }
cpkt_preset() { cpkt_configuration=Debug; }
cpkt_locked() { :; }
cpkt_info() {
  printf '%s\\n' x86_64-linux-gnu x86_64-linux-musl aarch64-linux-gnu aarch64-linux-musl armhf-linux-gnu armhf-linux-musl arm64-apple-darwin
}
''')
    (root / 'scripts/build.sh').write_text('''#!/usr/bin/env bash
printf '%s\\n' "$*" >> "$(dirname "$0")/../calls"
''')
    env = {k: v for k, v in os.environ.items()
           if k not in ('GROUP', 'PRESET', 'SCOPE', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL')}
    calls = root / 'calls'

    def run(*args, success=True):
        calls.unlink(missing_ok=True)
        result = subprocess.run(['make', '--no-print-directory', *args], cwd=root,
                                env=env, text=True, capture_output=True)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return calls.read_text().splitlines() if calls.exists() else []

    native = 'arm64-apple-darwin-debug' if os.uname().sysname == 'Darwin' else 'debug'
    assert run('build') == ['build --group all --preset ' + native]
    assert run('test') == ['test --group all --preset ' + native]
    assert run('test', 'PRESET=aarch64-linux-musl-release') == [
        'test --group all --preset aarch64-linux-musl-release']
    for action, mode in [('build-release', 'build'), ('cross-build', 'build'),
                         ('test-cross', 'test'), ('cross-test', 'test')]:
        assert run(action) == [mode + ' --group all --preset ' + target + '-release'
                               for target in ('x86_64-linux-gnu', 'x86_64-linux-musl',
                                              'aarch64-linux-gnu', 'aarch64-linux-musl',
                                              'armhf-linux-gnu', 'armhf-linux-musl')]
    assert run('build', 'GROUP=db', success=False) == []
print('Native defaults, explicit presets, matrices and owner rejection passed')
