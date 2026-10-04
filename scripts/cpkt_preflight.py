#!/usr/bin/env python3
"""Standalone selected compiler/recipe fixtures, without any SDK graph."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

from cpkt_cmake_inputs import commands
from cpkt_inventory import load, record
from cpkt_operation import operation_fds, delegated, run
from cpkt_preflight_runner import run_registered_fixtures

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--group', required=True)
parser.add_argument('--target', required=True)
parser.add_argument('--preset', required=True)
parser.add_argument('--toolchain', type=Path)
args = parser.parse_args()
root = args.root.resolve()
try:
    if 'CPKT_OPERATION_FD' not in os.environ:
        sys.exit(run(root, args.group, [sys.executable, __file__, *sys.argv[1:]]))
    fd, _ = delegated(root, args.group)
    data = load(root)
    profile = ('native-darwin' if sys.platform == 'darwin' else 'osxcross') if args.target.endswith('darwin') else (
        'native-linux' if args.target.startswith('x86_64') else 'linux-runner')
    cases = [name for name in data['preflight'][profile]['required']
             if args.group == 'all' or record(data['tests'], name)['group'] in (args.group, 'tooling')]
    definitions = {}
    for name, arguments, definition in commands((root/'CMakeLists.txt').read_text()):
        if name == 'cpkt_group_add_test':
            tokens = arguments.split()
            if tokens[:1] == ['NAME']:
                definitions[tokens[1]] = definition
    selected = []
    for case in cases:
        keys = [key for key in definitions if re.fullmatch(re.sub(r'\$\{[^}]+\}', '(.+)', key), case)]
        if case in keys:
            keys = [case]
        elif keys:
            strongest = max(len(re.sub(r'\$\{[^}]+\}','',key)) for key in keys)
            keys = [key for key in keys if len(re.sub(r'\$\{[^}]+\}','',key)) == strongest]
        if len(keys) != 1:
            raise RuntimeError('missing/ambiguous standalone fixture definition: ' + case)
        key = keys[0]
        definition = definitions[key]
        variables = re.findall(r'\$\{([^}]+)\}', key)
        values = re.fullmatch(re.sub(r'\$\{[^}]+\}', '(.+)', key), case).groups()
        for variable, value in zip(variables, values):
            definition = definition.replace('${'+variable+'}', value)
            # Its concrete generator name is explicit in the loop registration.
            if variable == '_cpkt_install_generator':
                definition = definition.replace('${_cpkt_install_generator_name}', 'Unix Makefiles' if value == 'Makefiles' else value)
        selected.append(definition.replace('${CMAKE_SOURCE_DIR}', '${CPKT_REPO_ROOT}'))
    directory = root/'build/control/preflight'/args.target
    source = directory/'source'
    source.mkdir(parents=True, exist_ok=True)
    cmake = '''cmake_minimum_required(VERSION 3.21)
project(cpkt_preflight C CXX)
include(CTest)
set(CPKT_REPO_ROOT "ROOT")
set(CPKT_TARGET_ID "TARGET")
set(CPKT_TARGET_LIBC "LIBC")
set(Python3_EXECUTABLE "PYTHON")
include("ROOT/cmake/CpktLocalRuntime.cmake")
set(CPKT_TEST_EXECUTABLE_PREFIX "${CMAKE_CROSSCOMPILING_EMULATOR}")
function(cpkt_group_add_test)
  cmake_parse_arguments(PARSE_ARGV 0 fixture "" "NAME" "COMMAND")
  add_test(NAME "${fixture_NAME}" COMMAND "PYTHON" "ROOT/scripts/cpkt_helper_dispatch.py"
    --root "ROOT" --group "GROUP" --target "TARGET" --test "${fixture_NAME}"
    --binary "${CMAKE_BINARY_DIR}" -- ${fixture_COMMAND})
endfunction()
'''
    for key, value in {'ROOT':str(root),'TARGET':args.target,'LIBC':args.target.rsplit('-',1)[-1],
                       'PYTHON':sys.executable,'GROUP':args.group}.items():
        cmake = cmake.replace('"'+key+'"','"'+value+'"').replace(key+'/',value+'/')
    (source/'CMakeLists.txt').write_text(cmake+'\n'.join(selected)+'\n')
    command = [os.environ.get('CMAKE','cmake'), '-S', str(source), '-B', str(directory/'binary'), '-G','Ninja']
    if args.toolchain:
        command += ['-DCMAKE_TOOLCHAIN_FILE='+str(args.toolchain)]
    if args.preset in ('arm64-apple-darwin-native','arm64-apple-darwin-debug'):
        command += ['-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0']
    subprocess.run(command, check=True, pass_fds=operation_fds())
    if cases:
        run_registered_fixtures(directory/'binary', cases, os.environ.get('CTEST', 'ctest'))
    print('standalone preflight passed: '+args.target+' '+','.join(cases))
except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
    sys.exit(str(error))
