#!/usr/bin/env python3
"""A clean CTest tree must reject missing selected executables before execution."""

from pathlib import Path
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
scratch = repo / 'build'
scratch.mkdir(exist_ok=True)
helper = repo / 'scripts/run-selected-tests.py'
with tempfile.TemporaryDirectory(prefix='selected-tests-', dir=scratch) as temporary:
    root = Path(temporary)
    binary = root / 'configured'
    first = root / 'first'
    second = root / 'second'
    marker = root / 'executed'
    first.write_text('#!/bin/sh\ntouch "' + str(marker) + '"\n')
    first.chmod(0o755)
    (root / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(selection NONE)\n'
        'enable_testing()\n'
        'add_test(NAME selected_first COMMAND "' + str(first) + '")\n'
        'add_test(NAME selected_second COMMAND "' + str(second) + '")\n'
        'add_test(NAME unrelated_missing COMMAND "' + str(root / 'absent') + '")\n')
    subprocess.run(['cmake', '-S', str(root), '-B', str(binary)], check=True,
                   capture_output=True, text=True)

    def run(regex='^selected_', verbose=False):
        command = [sys.executable, str(helper), '--test-dir', str(binary),
                   '--regex', regex]
        if verbose:
            command.append('--verbose')
        return subprocess.run(command, capture_output=True, text=True)

    result = run()
    assert result.returncode == 2 and 'selected_second' in result.stderr, result
    assert not marker.exists(), 'a test ran before the missing executable was rejected'
    second.write_text('#!/bin/sh\nexit 0\n')
    second.chmod(0o644)
    result = run()
    assert result.returncode == 2 and 'selected_second' in result.stderr, result
    assert not marker.exists()
    second.chmod(0o755)
    result = run(verbose=True)
    assert result.returncode == 0 and '2 selected tests' in result.stdout, result
    assert marker.exists()
    marker.unlink()
    result = run('^nothing_matches$')
    assert result.returncode != 0, result
    assert not marker.exists()
    second.unlink()
    result = run('^selected_first$')
    assert result.returncode == 0 and marker.exists(), result
    second.write_text('#!/bin/sh\nexit 1\n')
    second.chmod(0o755)
    result = run()
    assert result.returncode != 0 and 'selected_second' in result.stdout, result
print('Selected CTest commands fail early when absent/non-executable; '
      'selection, execution, and test failure propagation verified')
