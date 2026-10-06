#!/usr/bin/env python3
"""Host-shell behavior and real bootstrap selection; no dependency production."""
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from native_lifecycle_fixture import environment, seed

ROOT = Path(__file__).resolve().parents[1]


class HostBash(unittest.TestCase):
    def setUp(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        self.scratch = tempfile.TemporaryDirectory(prefix='host-bash-', dir=ROOT / 'build')
        self.work = Path(self.scratch.name)
        self.env = environment()

    def tearDown(self):
        self.scratch.cleanup()

    def run_command(self, args, *, cwd=None, env=None):
        return subprocess.run(list(map(str, args)), cwd=cwd or ROOT,
                              env=env or self.env, capture_output=True, text=True)

    def test_version_boundary_and_malformed_inputs(self):
        cases = [('3', '2', False), ('4', '0', False), ('4', '3', False),
                 ('4', '4', True), ('4', '44', True), ('5', '0', True),
                 ('6', '1', True), ('04', '04', True), ('0', '99', False),
                 ('', '4', False), ('4', '', False), ('4.4', '0', False),
                 ('4', '-1', False), ('x', '4', False), ('4', '4x', False)]
        for major, minor, supported in cases:
            with self.subTest(major=major, minor=minor):
                result = self.run_command(['bash', '-euc',
                    'source "$1"; if cpkt_host_bash_supported "$2" "$3"; then printf supported; else printf unsupported; fi',
                    'version-fixture', ROOT / 'scripts/require-host-bash.sh', major, minor])
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertEqual('supported' if supported else 'unsupported', result.stdout)

    def test_selected_shell_provenance_and_empty_array_argv(self):
        result = self.run_command(['bash', ROOT / 'scripts/require-host-bash.sh'])
        self.assertEqual(0, result.returncode, result.stderr)
        print(result.stdout.strip(), flush=True)
        result = self.run_command(['bash', '-euc',
            'source "$1"; count() { printf "%s\n" "$#"; }; arguments=(); count "${arguments[@]}"; arguments=(""); count "${arguments[@]}"',
            'argv-fixture', ROOT / 'scripts/require-host-bash.sh'])
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual('0\n1\n', result.stdout)

    def test_native_wrapper_preserves_zero_and_one_empty_argument(self):
        repo = self.work / 'repo'
        seed(repo)
        graph = repo / 'build/graph'
        graph.mkdir(parents=True)
        native = repo / 'native'
        native.write_text('#!/usr/bin/env bash\nset -euo pipefail\nprintf "shell=%s\nargc=%s\n" "$BASH_VERSION" "$#"\nfor arg in "$@"; do printf "arg=<%s>\n" "$arg"; done\n')
        native.chmod(0o755)
        (graph / 'CMakeCache.txt').write_text('CMAKE_PROJECT_NAME:STATIC=argv_fixture\nCPKT_NATIVE_MAKE_PROGRAM:FILEPATH=' + str(native) + '\n')
        (graph / 'cpkt-generated-outputs.txt').write_bytes(b'')
        for args, expected in (([], 'argc=0\n'), ([''], 'argc=1\narg=<>\n'), (['--version'], 'argc=1\narg=<--version>\n')):
            with self.subTest(args=args):
                result = self.run_command(['bash', repo / 'scripts/operation.sh', '--group', 'all', '--',
                    repo / 'scripts/native-build.sh', *args], cwd=graph)
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                self.assertTrue(result.stdout.startswith('shell='), result.stdout)
                self.assertTrue(result.stdout.endswith(expected), result.stdout)
        (graph / 'cpkt-generated-outputs.txt').unlink()
        result = self.run_command(['bash', repo / 'scripts/operation.sh', '--group', 'all', '--',
            repo / 'scripts/native-build.sh', '--version'], cwd=graph)
        self.assertNotEqual(0, result.returncode)
        self.assertIn('Required generated-output inventory is missing', result.stderr)
        self.assertNotIn('shell=', result.stdout)

    def test_actual_stock_bash_rejects_before_children_or_state(self):
        stock = Path('/bin/bash')
        result = self.run_command([stock, '-c', 'printf "%s.%s" "${BASH_VERSINFO[0]}" "${BASH_VERSINFO[1]}"'])
        major, minor = map(int, result.stdout.split('.'))
        if (major, minor) >= (4, 4):
            self.skipTest('actual /bin/bash is ' + result.stdout + '; stock macOS Bash 3.2 unavailable here')
        print('Actual unsupported /bin/bash: ' + result.stdout, flush=True)
        repo = self.work / 'repo'
        seed(repo)
        tools = self.work / 'tools'
        tools.mkdir()
        marker = self.work / 'child-ran'
        for name in ('cmake', 'python3', 'ninja', 'curl', 'flock'):
            tool = tools / name
            tool.write_text('#!/bin/sh\nprintf child > ' + shlex.quote(str(marker)) + '\nexit 99\n')
            tool.chmod(0o755)
        env = dict(self.env, PATH=str(tools) + os.pathsep + self.env['PATH'],
                   CPKT_TOOLCHAIN_CACHE=str(self.work / 'toolcache'),
                   CPKT_DEPENDENCY_CACHE=str(self.work / 'depcache'))
        for name, args in (('build.sh', ['configure']), ('operation.sh', ['--group', 'all', '--', 'ninja']),
                           ('native-build.sh', ['--version']), ('helper.sh', []),
                           ('build-guard.sh', [str(repo), 'all', 'ninja']),
                           ('cpkt-toolchains.sh', ['discover', 'x86_64-linux-gnu']),
                           ('cpkt-aflpp.sh', ['discover', 'x86_64-linux-gnu'])):
            with self.subTest(script=name):
                result = self.run_command([stock, repo / 'scripts' / name, *args], cwd=repo, env=env)
                self.assertEqual(2, result.returncode, result.stdout + result.stderr)
                for message in ('host Bash >= 4.4', 'brew install bash', 'export PATH=', 'host bash package'):
                    self.assertIn(message, result.stderr)
                self.assertFalse(marker.exists(), name + ' ran a child')
                for path in ('build', '.cache'):
                    self.assertFalse((repo / path).exists(), name + ' mutated generated state')
                self.assertFalse((self.work / 'toolcache').exists())
                self.assertFalse((self.work / 'depcache').exists())

    def test_actual_workflow_bootstrap_selects_bash_for_make_and_native_wrapper(self):
        repo = self.work / 'repo'
        seed(repo)
        tools = self.work / 'tools'
        tools.mkdir()
        prefix = self.work / 'Homebrew Bash'
        (prefix / 'bin').mkdir(parents=True)
        (prefix / 'bin/bash').symlink_to(shutil.which('bash'))
        brew = tools / 'brew'
        fallback = ('exec ' + shlex.quote(shutil.which('brew')) + ' "$@"') if shutil.which('brew') else 'exit 90'
        brew.write_text('#!/bin/sh\nif [ "$1" = --prefix ] && [ "$2" = bash ]; then printf "%s\n" ' + shlex.quote(str(prefix)) + '; else ' + fallback + '; fi\n')
        brew.chmod(0o755)
        github_path = self.work / 'github-path'
        env = dict(self.env, PATH=str(tools) + os.pathsep + self.env['PATH'], GITHUB_PATH=str(github_path))
        text = (ROOT / '.github/workflows/darwin-bundle.yml').read_text()
        matches = re.findall(r'      - name: Select host Bash\n        shell: [^\n]+\n        run: \|\n((?:          [^\n]*\n)+)', text)
        self.assertEqual(1, len(matches), 'the source job needs its own stock-compatible PATH bootstrap')
        for lines in matches:
            bootstrap = self.work / 'bootstrap.sh'
            bootstrap.write_text(''.join(line[10:] + '\n' for line in lines.splitlines()))
            result = self.run_command(['/bin/bash', '--noprofile', '--norc', '-e', '-o', 'pipefail', bootstrap], cwd=repo, env=env)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn(str(prefix / 'bin/bash'), result.stdout)
            print('Bootstrap selected: ' + result.stdout.strip(), flush=True)
        selected = github_path.read_text().splitlines()
        self.assertEqual([str(prefix / 'bin')], selected)
        # Apply the actual GITHUB_PATH result as the next worker step does.
        env['PATH'] = selected[0] + os.pathsep + env['PATH']
        (repo / 'Makefile').write_text('SHELL := bash\n.SHELLFLAGS := -euo pipefail -c\nprobe:\n\t@printf "make-shell=%s (%s)\\n" "$${BASH_VERSION}" "$${BASH}"\n\t@bash scripts/operation.sh --group all -- scripts/native-build.sh --version\n')
        native = repo / 'native'
        native.write_text('#!/usr/bin/env bash\nprintf "native-shell=%s (%s)\n" "$BASH_VERSION" "$BASH"\n')
        native.chmod(0o755)
        env['CPKT_NATIVE_MAKE_PROGRAM'] = str(native)
        result = self.run_command(['make', '--no-print-directory', 'probe'], cwd=repo, env=env)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertEqual(2, result.stdout.count(str(prefix / 'bin/bash')), result.stdout)
        self.assertIn('make-shell=', result.stdout)
        self.assertIn('native-shell=', result.stdout)
        print(result.stdout.strip(), flush=True)

    def test_basename_native_wrapper_loads_the_guard(self):
        repo=self.work/'basename-repo'
        seed(repo)
        native=repo/'native'
        native.write_text('#!/bin/sh\nprintf native-version\n')
        native.chmod(0o755)
        env=dict(self.env,CPKT_NATIVE_MAKE_PROGRAM=str(native))
        result=self.run_command(['bash',repo/'scripts/operation.sh','--group','all','--',
                                'bash','native-build.sh','--version'],cwd=repo/'scripts',env=env)
        self.assertEqual(0,result.returncode,result.stdout+result.stderr)
        self.assertEqual('',result.stderr,'basename invocation failed to load the startup guard')
        self.assertEqual('native-version',result.stdout)

    def test_missing_guard_fails_before_children_or_state(self):
        repo=self.work/'missing-guard-repo'
        seed(repo)
        (repo/'scripts/require-host-bash.sh').unlink()
        tools=self.work/'missing-guard-tools'
        tools.mkdir()
        marker=self.work/'missing-guard-child'
        for name in ('cmake','python3','ninja','curl','flock'):
            tool=tools/name
            tool.write_text('#!/bin/sh\nprintf child > '+shlex.quote(str(marker))+'\nexit 99\n')
            tool.chmod(0o755)
        env=dict(self.env,PATH=str(tools)+os.pathsep+self.env['PATH'])
        for name,args in (('native-build.sh',['--version']),('helper.sh',[]),
                          ('build-guard.sh',[str(repo),'all','ninja']),
                          ('operation.sh',['--group','all','--','ninja']),
                          ('cpkt-toolchains.sh',['discover','x86_64-linux-gnu'])):
            for entry in (name,str(repo/'scripts'/name)):
                with self.subTest(script=entry):
                    marker.unlink(missing_ok=True)
                    result=self.run_command(['bash',entry,*args],cwd=repo/'scripts',env=env)
                    self.assertNotEqual(0,result.returncode)
                    self.assertIn('require-host-bash.sh',result.stderr)
                    self.assertFalse(marker.exists(),'missing guard allowed a child to run')
                    self.assertFalse((repo/'build').exists())
                    self.assertFalse((repo/'.cache').exists())

    def test_declared_preflight_tuple_is_validated(self):
        helper = ROOT / 'cmake/CpktPreflightTuple.cmake'
        for target in ('x86_64-linux-gnu', 'x86_64-linux-musl', 'aarch64-linux-gnu',
                       'aarch64-linux-musl', 'armhf-linux-gnu', 'armhf-linux-musl', 'arm64-apple-darwin'):
            arch, _, suffix = target.split('-')
            values = dict(CPKT_TARGET_ID=target, CPKT_TARGET_ARCH=arch,
                          CPKT_TARGET_OS='darwin' if suffix == 'darwin' else 'linux',
                          CPKT_TARGET_LIBC='' if suffix == 'darwin' else suffix)
            for bad in (None, 'ARCH', 'OS', 'LIBC'):
                with self.subTest(target=target, bad=bad):
                    declared = dict(values)
                    if bad:
                        declared['CPKT_TARGET_' + bad] = 'incorrect'
                    result = self.run_command(['cmake', *('-D' + key + '=' + value for key, value in declared.items()), '-P', helper])
                    if bad:
                        self.assertNotEqual(0, result.returncode)
                        self.assertIn('Preflight target tuple mismatch', result.stderr)
                    else:
                        self.assertEqual(0, result.returncode, result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
