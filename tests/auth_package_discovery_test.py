#!/usr/bin/env python3
"""Authentication packages must discover adjacent SDK OpenSSL on their own."""

import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('repo', type=Path)
parser.add_argument('--scratch', required=True, type=Path)
parser.add_argument('--compiler', required=True)
parser.add_argument('--toolchain', default='')
parser.add_argument('--sdk-prefix', type=Path)
args = parser.parse_args()
args.scratch.mkdir(parents=True, exist_ok=True)

with tempfile.TemporaryDirectory(prefix='auth-discovery-', dir=args.scratch) as work:
    root = Path(work).resolve()
    prefix = args.sdk_prefix.resolve() if args.sdk_prefix else root / 'sdk'
    if args.sdk_prefix is None:
        (prefix / 'include').mkdir(parents=True)
        openssl = prefix / 'lib/cmake/OpenSSL'
        openssl.mkdir(parents=True)
        (openssl / 'OpenSSLConfig.cmake').write_text('''
get_filename_component(_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
foreach(component IN ITEMS SSL Crypto)
  add_library(OpenSSL::${component} STATIC IMPORTED)
  set_target_properties(OpenSSL::${component} PROPERTIES
    IMPORTED_LOCATION "${_prefix}/lib/fixture-${component}.a")
endforeach()
set(OpenSSL_FOUND TRUE)
''')
        source = (args.repo / 'cmake/package_metadata.cmake').read_text()
        for package in ('CpktGssapi', 'Kerberos5'):
            start = source.index('cpkt_metadata_file(WRITE "${_stage_root}/lib/cmake/' + package + '/')
            end = source.index('\n)\n', start) + 3
            (prefix / 'lib/cmake' / package).mkdir()
            generator = root / (package + '-generate.cmake')
            generator.write_text('set(_stage_root "' + str(prefix) + '")\n'
                                 'set(_cpkt_static_library_suffix .a)\n'
                                 'set(_cpkt_shared_library_suffix .so)\n'
                                 + source[start:end].replace('cpkt_metadata_file(', 'file('))
            subprocess.run(['cmake', '-P', str(generator)], check=True,
                           capture_output=True, text=True)
        relocated = root / 'relocated sdk'
        prefix.rename(relocated)
        prefix = relocated
    decoy = root / 'host/lib/cmake/OpenSSL'
    decoy.mkdir(parents=True)
    (decoy / 'OpenSSLConfig.cmake').write_text(
        'message(FATAL_ERROR "Host OpenSSL selected instead of SDK OpenSSL")\n')
    for package in ('CpktGssapi', 'Kerberos5'):
        for mode in ('isolated', 'host-decoy'):
            consumer = root / (package + '-' + mode)
            consumer.mkdir()
            (consumer / 'CMakeLists.txt').write_text('''
cmake_minimum_required(VERSION 3.21)
project(auth_discovery LANGUAGES C)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)
set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH FALSE)
find_package(''' + package + ''' CONFIG REQUIRED)
if(NOT OpenSSL_DIR STREQUAL "${CPKT_EXPECTED_PREFIX}/lib/cmake/OpenSSL")
  message(FATAL_ERROR "Authentication package did not select adjacent OpenSSL")
endif()
get_target_property(ssl_location OpenSSL::SSL IMPORTED_LOCATION)
string(FIND "${ssl_location}" "${CPKT_EXPECTED_PREFIX}/lib/" in_sdk)
if(NOT in_sdk EQUAL 0)
  message(FATAL_ERROR "OpenSSL target points outside SDK: ${ssl_location}")
endif()
''')
            command = ['cmake', '-S', str(consumer), '-B', str(consumer / 'build'),
                       '-DCMAKE_C_COMPILER=' + args.compiler,
                       '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY',
                       '-D' + package + '_DIR=' + str(prefix / 'lib/cmake' / package),
                       '-DCPKT_EXPECTED_PREFIX=' + str(prefix)]
            if args.toolchain:
                command.append('-DCMAKE_TOOLCHAIN_FILE=' + args.toolchain)
            if mode == 'host-decoy':
                command.append('-DCMAKE_PREFIX_PATH=' + str(root / 'host'))
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise SystemExit(package + ' ' + mode + ':\n' + result.stdout + result.stderr)
            print(package + ' ' + mode + ': adjacent SDK OpenSSL selected')
