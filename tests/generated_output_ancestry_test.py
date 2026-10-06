#!/usr/bin/env python3
"""Exercise real generator entrypoints and native output writers in tiny graphs."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from cpkt_inventory import REPOSITORY_GROUP


class GeneratedOutputMutation(unittest.TestCase):
    redirects = 0

    def run_command(self, command, cwd):
        from native_lifecycle_fixture import environment
        result = subprocess.run(list(map(str, command)), cwd=cwd, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                env=environment())
        return result

    def identity(self, path):
        stat = path.lstat()
        return (path.read_bytes(), stat.st_mode, stat.st_ino, stat.st_mtime_ns)

    def seed(self, work):
        from native_lifecycle_fixture import seed
        repo = work / 'repo'
        seed(repo)
        for directory in ('tools', 'tests'):
            if (ROOT / directory).is_dir():
                # Only maintained generator inputs, never dependencies or SDKs.
                if directory == 'tools':
                    shutil.copytree(ROOT / directory, repo / directory)
                else:
                    (repo / directory).mkdir()
        foreign = work / 'foreign'
        foreign.mkdir()
        sentinel = foreign / 'sentinel'
        sentinel.write_bytes(b'foreign bytes\n')
        sentinel.chmod(0o640)
        return repo, foreign, sentinel

    def operation(self, repo, command):
        return self.run_command(['bash', repo / 'scripts/operation.sh',
                                 '--group', REPOSITORY_GROUP, '--', *command], repo)

    def redirect(self, path, kind, foreign, sentinel):
        if kind == 'directory':
            link = path.parent
        elif kind == 'ancestor':
            link = path.parent.parent
        else:
            link = path
        link.parent.mkdir(parents=True, exist_ok=True)
        link.symlink_to(foreign if kind in ('directory', 'ancestor') else
                        foreign / 'absent' if kind == 'dangling' else sentinel)
        return link

    def assert_refused(self, result, link, foreign, sentinel, before):
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertIn('symlink ancestor', result.stdout.lower())
        self.assertIn(str(link), result.stdout)
        self.assertTrue(link.is_symlink())
        self.assertEqual(before, self.identity(sentinel))
        self.assertEqual([sentinel], list(foreign.iterdir()))
        type(self).redirects += 1

    def helper(self, repo):
        text = (ROOT / 'CMakeLists.txt').read_text()
        match = re.search(r'^function\(cpkt_apply_auth_export_catalog target_name catalog_name\)\n.*?^endfunction\(\)', text, re.M | re.S)
        self.assertIsNotNone(match)
        return match.group()

    def test_export_helper_refuses_both_formats_before_mutation(self):
        for system, suffix in (('Linux', 'map'), ('Darwin', 'exports')):
            for kind in ('leaf', 'dangling', 'directory', 'ancestor'):
                with self.subTest(system=system, kind=kind), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='generated-export-') as tmp:
                    repo, foreign, sentinel = self.seed(Path(tmp))
                    (repo / 'cmake/exports').mkdir(exist_ok=True)
                    (repo / 'cmake/exports/cpkt_probe.txt').write_text('cpkt_probe\n')
                    output = repo / 'build/graph/owned' / ('cpkt_probe.' + suffix)
                    link = self.redirect(output, kind, foreign, sentinel)
                    (repo / 'probe.c').write_text('int cpkt_probe(void) { return 1; }\n')
                    (repo / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(probe C)\n'
                        'set(CMAKE_BINARY_DIR "' + str(output.parent) + '")\n'
                        'set(CMAKE_SYSTEM_NAME ' + system + ')\n' + self.helper(repo) + '\n'
                        'add_library(probe SHARED probe.c)\ncpkt_apply_auth_export_catalog(probe cpkt_probe)\n'
                        'file(WRITE "' + str(repo / 'child-marker') + '" child)\n')
                    before = self.identity(sentinel)
                    result = self.operation(repo, ['cmake', '-S', repo, '-B', repo / 'build/configure', '-G', 'Ninja'])
                    self.assert_refused(result, link, foreign, sentinel, before)
                    self.assertFalse((repo / 'child-marker').exists())

    def test_export_configure_success_and_idle_parity(self):
        for system, suffix in (('Linux', 'map'), ('Darwin', 'exports')):
            with self.subTest(system=system), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='generated-export-good-') as tmp:
                repo, foreign, sentinel = self.seed(Path(tmp))
                (repo / 'cmake/exports').mkdir(exist_ok=True)
                symbols = ['cpkt_probe_alpha', 'cpkt_probe_beta']
                (repo / 'cmake/exports/cpkt_probe.txt').write_text('\n'.join(symbols) + '\n')
                (repo / 'probe.c').write_text('int cpkt_probe_alpha(void) { return 1; }\n')
                (repo / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(probe C)\n'
                    'set(CMAKE_SYSTEM_NAME ' + system + ')\n' + self.helper(repo) + '\n'
                    'add_library(probe SHARED probe.c)\ncpkt_apply_auth_export_catalog(probe cpkt_probe)\ncpkt_write_generated_output_inventory()\n')
                command = ['cmake', '-S', repo, '-B', repo / 'build/graph', '-G', 'Ninja']
                result = self.operation(repo, command)
                self.assertEqual(0, result.returncode, result.stdout)
                output = repo / 'build/graph' / ('cpkt_probe.' + suffix)
                expected = ''.join('_' + name + '\n' for name in symbols) if system == 'Darwin' else '{\n  global:\n' + ''.join('    ' + name + ';\n' for name in symbols) + '  local: *;\n};\n'
                self.assertEqual(expected, output.read_text())
                before = self.identity(output)
                result = self.operation(repo, command)
                self.assertEqual(0, result.returncode, result.stdout)
                self.assertEqual(before, self.identity(output))

    def facade_command(self, repo, name, include, header, source):
        if name == 'cmocka':
            return [sys.executable, repo / 'tools/generate_cmocka_c89.py',
                    '--header', include / 'cmocka.h', '--output', header]
        return [sys.executable, repo / ('tools/generate_' + name + '_c89_facade.py'),
                '--include-dir', include, '--header', header, '--source', source]

    def test_core_facade_families_preflight_all_outputs(self):
        if REPOSITORY_GROUP != 'core':
            self.skipTest('core owns these generator families')
        for name in ('nghttp2', 'libssh2', 'mqttc', 'lua', 'cmocka'):
            leaves = ('header.h', 'source.c') if name != 'cmocka' else ('header.h', 'cmocka_types.h', 'cmocka_bridge.inc', 'cmocka_bridge_exports.txt')
            for leaf in leaves:
                for kind in ('leaf', 'dangling', 'directory', 'ancestor'):
                    with self.subTest(name=name, leaf=leaf, kind=kind), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='generated-facade-') as tmp:
                        repo, foreign, sentinel = self.seed(Path(tmp))
                        base = repo / 'build/generated' / name
                        output = base / leaf
                        link = self.redirect(output, kind, foreign, sentinel)
                        before = self.identity(sentinel)
                        command = self.facade_command(repo, name, repo / 'fake-input', base / 'header.h', base / 'source.c')
                        result = self.operation(repo, command)
                        self.assert_refused(result, link, foreign, sentinel, before)
                        if kind in ('leaf', 'dangling'):
                            self.assertEqual([link], list(base.iterdir()))

    def test_core_warm_siblings_preserved_before_later_output_refusal(self):
        if REPOSITORY_GROUP != 'core':
            self.skipTest('core owns these facade families')
        for name in ('nghttp2', 'libssh2', 'mqttc', 'lua', 'cmocka'):
            with self.subTest(name=name), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='generated-warm-') as tmp:
                repo, foreign, sentinel = self.seed(Path(tmp))
                base = repo / 'build/generated' / name
                base.mkdir(parents=True)
                header = base / 'header.h'
                source = base / 'source.c'
                siblings = [header] if name != 'cmocka' else [header, base / 'cmocka_types.h', base / 'cmocka_bridge.inc']
                for index, sibling in enumerate(siblings):
                    sibling.write_text('existing output ' + str(index) + '\n')
                    sibling.chmod(0o600)
                snapshots = [self.identity(path) for path in siblings]
                link = self.redirect(base / 'cmocka_bridge_exports.txt' if name == 'cmocka' else source,
                                     'leaf', foreign, sentinel)
                before = self.identity(sentinel)
                result = self.operation(repo, self.facade_command(repo, name, repo / 'missing-input', header, source))
                self.assert_refused(result, link, foreign, sentinel, before)
                self.assertEqual(snapshots, [self.identity(path) for path in siblings])

    def tiny_nghttp2(self, include):
        native = include / 'nghttp2'
        native.mkdir(parents=True)
        header = 'typedef ptrdiff_t nghttp2_ssize;\n' + ''.join(
            'NGHTTP2_EXTERN int nghttp2_probe_' + str(index) + '(void);\n' for index in range(181)) + '#ifdef __cplusplus\n}\n#endif\n#endif /* NGHTTP2_H */\n'
        (native / 'nghttp2.h').write_text(header)
        (native / 'nghttp2ver.h').write_text('#define NGHTTP2_VERSION "tiny"\n#define NGHTTP2_VERSION_NUM 1\n')

    def native_tool(self, repo, generator):
        native = shutil.which('ninja' if generator == 'Ninja' else 'make')
        tool = repo / 'build/native-recorder'
        tool.parent.mkdir(parents=True, exist_ok=True)
        tool.write_text('#!/bin/sh\nprintf "call %s\\n" "$*" >> "' + str(repo / 'native-calls') + '"\nexec "' + native + '" "$@"\n')
        tool.chmod(0o755)
        return tool

    def native_context(self, repo, tool, producer=False):
        return ('set(CPKT_NATIVE_MAKE_PROGRAM "' + str(tool) + '" CACHE FILEPATH "")\n'
                'set(ENV{CPKT_NATIVE_MAKE_PROGRAM} "${CPKT_NATIVE_MAKE_PROGRAM}")\n'
                'set(CMAKE_MAKE_PROGRAM "${CMAKE_SOURCE_DIR}/scripts/native-build.sh" CACHE FILEPATH "" FORCE)\n'
                'set(CPKT_DEPENDENCY_PRODUCER ' + ('ON' if producer else 'OFF') + ' CACHE BOOL "")\n'
                'set(CPKT_GROUP ' + REPOSITORY_GROUP + ' CACHE STRING "")\n'
                'set(CPKT_TARGET_ID fixture-target CACHE STRING "")\n')

    def native_evidence(self, repo):
        evidence = repo / 'build/verification/fixture-target' / REPOSITORY_GROUP
        evidence.mkdir(parents=True, exist_ok=True)
        names = ('Debug-development.json', 'Debug-built.json', 'Release-development.json',
                 'Release-built.json', 'component-toy.json')
        paths = [evidence / name for name in names]
        sibling = evidence.parent / 'unselected/Debug-development.json'
        sibling.parent.mkdir(exist_ok=True)
        paths.append(sibling)
        for path in paths:
            path.write_text('existing evidence ' + path.name + '\n')
            path.chmod(0o600)
        return {path: self.identity(path) for path in paths}

    def assert_native_untouched(self, repo, evidence, foreign, sentinel, before):
        self.assertFalse((repo / 'native-calls').exists())
        self.assertFalse((repo / 'child-marker').exists())
        self.assertEqual(evidence, {path: self.identity(path) for path in evidence})
        self.assertEqual(before, self.identity(sentinel))
        self.assertEqual([sentinel], list(foreign.iterdir()))

    def test_authenticated_native_facade_target_refusal_and_success(self):
        if REPOSITORY_GROUP != 'core':
            self.skipTest('core owns nghttp2 native generation')
        for kind in ('good', 'directory', 'ancestor', 'header', 'source', 'dangling-header', 'dangling-source', 'missing', 'missing-redirect'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='native-generated-') as tmp:
                repo, foreign, sentinel = self.seed(Path(tmp))
                tool = self.native_tool(repo, 'Ninja')
                include = repo / 'tiny-sdk/include'
                self.tiny_nghttp2(include)
                # An immutable input symlink is legitimate.
                alias = repo / 'sdk-link'
                alias.symlink_to(include, target_is_directory=True)
                text = (ROOT / 'CMakeLists.txt').read_text()
                command = re.search(r'add_custom_command\(\n  OUTPUT "\$\{CPKT_NGHTTP2_FACADE_HEADER\}".*?\n  VERBATIM\)', text, re.S).group()
                graph = repo / 'build/graph'
                (repo / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(probe NONE)\n'
                    'add_library(cpkt::nghttp2_static INTERFACE IMPORTED)\n'
                    'set_target_properties(cpkt::nghttp2_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "' + str(alias) + '")\n'
                    'include(cmake/CpktMutationPaths.cmake)\n'
                    + self.native_context(repo, tool) +
                    'set(CPKT_PYTHON3_EXECUTABLE "' + sys.executable + '")\n'
                    'set(CPKT_NGHTTP2_FACADE_NATIVE_HEADERS "' + str(alias / 'nghttp2/nghttp2.h') + '" "' + str(alias / 'nghttp2/nghttp2ver.h') + '")\n'
                    'set(CPKT_NGHTTP2_FACADE_HEADER "${CMAKE_BINARY_DIR}/generated/nghttp2/include/cpkt/nghttp2.h")\n'
                    'set(CPKT_NGHTTP2_FACADE_SOURCE "${CMAKE_BINARY_DIR}/generated/nghttp2/src/nghttp2.c")\n'
                    'cpkt_register_generated_outputs(\"${CPKT_NGHTTP2_FACADE_HEADER}\" \"${CPKT_NGHTTP2_FACADE_SOURCE}\")\n'
                    'cpkt_write_generated_output_inventory()\n'
                    + command + '\nadd_custom_target(private_child COMMAND \"${CMAKE_COMMAND}\" -E touch \"${CMAKE_SOURCE_DIR}/child-marker\")\nadd_custom_target(facade DEPENDS "${CPKT_NGHTTP2_FACADE_HEADER}" "${CPKT_NGHTTP2_FACADE_SOURCE}")\nadd_dependencies(facade private_child)\n')
                result = self.operation(repo, ['cmake', '-S', repo, '-B', graph, '-G', 'Ninja'])
                self.assertEqual(0, result.returncode, result.stdout)
                header = graph / 'generated/nghttp2/include/cpkt/nghttp2.h'
                source = graph / 'generated/nghttp2/src/nghttp2.c'
                evidence = self.native_evidence(repo)
                (repo / 'native-calls').unlink(missing_ok=True)
                if kind.startswith('missing'):
                    (graph / 'cpkt-generated-outputs.txt').unlink()
                    if kind == 'missing-redirect':
                        self.redirect(graph / 'generated/nghttp2/leaf', 'directory', foreign, sentinel)
                    before = self.identity(sentinel)
                elif kind != 'good':
                    output = source if 'source' in kind else header
                    if kind == 'directory':
                        output = graph / 'generated/nghttp2/leaf'
                    elif kind == 'ancestor':
                        output = graph / 'generated/nghttp2/leaf'
                    redirect = 'dangling' if kind.startswith('dangling') else kind if kind in ('directory', 'ancestor') else 'leaf'
                    link = self.redirect(output, redirect, foreign, sentinel)
                    before = self.identity(sentinel)
                result = self.operation(repo, ['cmake', '--build', graph, '--target', 'facade'])
                if kind.startswith('missing'):
                    self.assertNotEqual(0, result.returncode, result.stdout)
                    self.assertIn('Required generated-output inventory is missing', result.stdout)
                    self.assertIn('scripts/build.sh configure', result.stdout)
                    self.assert_native_untouched(repo, evidence, foreign, sentinel, before)
                    self.assertFalse(header.exists())
                    self.assertFalse(source.exists())
                    result = self.operation(repo, [repo / 'scripts/native-build.sh', '-C', graph, 'clean'])
                    self.assertNotEqual(0, result.returncode, result.stdout)
                    self.assertIn('Required generated-output inventory is missing', result.stdout)
                    self.assert_native_untouched(repo, evidence, foreign, sentinel, before)
                elif kind == 'good':
                    self.assertEqual(0, result.returncode, result.stdout)
                    self.assertEqual(181, len(re.findall(r'^CPKT_NGHTTP2_API int cpkt_nghttp2_probe_', source.read_text(), re.M)))
                    self.assertEqual(181, len(re.findall(r'cpkt_nghttp2_probe_\d+\(void\)', header.read_text())))
                    identities = [self.identity(path) for path in (header, source)]
                    # Force real regeneration without changing contents.
                    os.utime(repo / 'tools/generate_nghttp2_c89_facade.py', None)
                    result = self.operation(repo, ['cmake', '--build', graph, '--target', 'facade'])
                    self.assertEqual(0, result.returncode, result.stdout)
                    self.assertIn('Generating', result.stdout)
                    self.assertEqual(identities, [self.identity(path) for path in (header, source)])
                else:
                    self.assert_refused(result, link, foreign, sentinel, before)
                    self.assertFalse((repo / 'child-marker').exists())
                    if kind in ('header', 'dangling-header'):
                        self.assertFalse(source.exists())
                    if kind in ('source', 'dangling-source'):
                        self.assertFalse(header.exists())

    def test_api_inventory_families_refuse_before_probe_or_partial_data(self):
        if REPOSITORY_GROUP != 'core':
            self.skipTest('core owns these inventories')
        for name in ('openssl', 'nghttp2', 'libssh2', 'mqttc', 'lua'):
            for leaf in ('report', 'probe') if name in ('openssl', 'nghttp2', 'libssh2') else ('report',):
                for kind in ('leaf', 'dangling', 'directory', 'ancestor'):
                    with self.subTest(name=name, leaf=leaf, kind=kind), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='generated-api-') as tmp:
                        repo, foreign, sentinel = self.seed(Path(tmp))
                        base = repo / 'build/probes/inventory'
                        output = base / 'result.json'
                        work = base / 'probe'
                        link = self.redirect(output if leaf == 'report' else work / (name + '-api-inventory.c'), kind, foreign, sentinel)
                        tool = repo / 'fake-child'
                        tool.write_text('#!/bin/sh\necho child > "' + str(repo / 'child-marker') + '"\nexit 1\n')
                        tool.chmod(0o755)
                        script = repo / ('tools/generate_' + name + '_api_inventory.py')
                        # Explicit CLI per owning family (no generic argv guesses).
                        args = ['--include-dir', repo / 'missing', '--facade-header', repo / 'missing-facade', '--output', output, '--symbol-tool', tool]
                        if name == 'lua':
                            args += ['--native-library', repo / 'native', '--facade-library', repo / 'facade', '--symbol-format', 'elf']
                        else:
                            args += ['--library', repo / 'native']
                        if name in ('openssl', 'nghttp2'):
                            args += ['--symbol-format', 'elf']
                        if name == 'openssl':
                            args += ['--num', repo / 'native.num']
                        if name in ('openssl', 'nghttp2', 'libssh2'):
                            args += ['--work-dir', work, '--clang', tool]
                        before = self.identity(sentinel)
                        result = self.operation(repo, [sys.executable, script, *args])
                        self.assert_refused(result, link, foreign, sentinel, before)
                        self.assertFalse((repo / 'child-marker').exists())
                        if leaf == 'report' and kind in ('leaf', 'dangling'):
                            self.assertFalse(work.exists())

    def test_db_inventory_and_compile_probe_preflight_before_children(self):
        for kind in ('leaf', 'dangling', 'directory', 'ancestor'):
            for leaf in ('preprocessed', 'report', 'compile-source', 'compile-object'):
                if leaf.startswith('compile') and REPOSITORY_GROUP != 'db':
                    continue
                with self.subTest(kind=kind, leaf=leaf), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='generated-db-api-') as tmp:
                    repo, foreign, sentinel = self.seed(Path(tmp))
                    base = repo / 'build/probe/owned'
                    output = base / ({'preprocessed':'native-sqlite.i', 'report':'native-sqlite.json', 'compile-source':'sqlite-native.c', 'compile-object':'sqlite-native.o'}[leaf])
                    link = self.redirect(output, kind, foreign, sentinel)
                    script = repo / 'probe.py'
                    script.write_text('import sys\nfrom pathlib import Path\nsys.path.insert(0,' + repr(str(repo / 'scripts')) + ')\n'
                        'import db_api_inventory as m\nm.output_dir=lambda target:Path(' + repr(str(base.parent)) + ')\n'
                        'def child(*args):\n Path(' + repr(str(repo / 'child-marker')) + ').write_text("child")\n raise RuntimeError("unexpected child")\n'
                        'm.compiler=child\n' +
                        ('import db_api_contract as c\nc.compile_probe("owned", "sqlite", [], native=True)\n' if leaf.startswith('compile') else 'm.inspect("owned", "sqlite")\n'))
                    if leaf.startswith('compile'):
                        # compile_probe has its own contract subtree.
                        script.write_text(script.read_text().replace('import db_api_contract as c\n', 'import db_api_contract as c\nm.output_dir=lambda target:Path(' + repr(str(base.parent / 'inventory')) + ')\n'))
                        # Its output path is <base.parent>/contract/owned.
                        actual = base.parent / 'contract/owned' / output.name
                        link.unlink()
                        link = self.redirect(actual, kind, foreign, sentinel)
                    before = self.identity(sentinel)
                    result = self.operation(repo, [sys.executable, script])
                    self.assert_refused(result, link, foreign, sentinel, before)
                    self.assertFalse((repo / 'child-marker').exists())

    def test_optional_opcua_all_native_outputs_preflight_before_scratch_or_children(self):
        if REPOSITORY_GROUP != 'misc':
            self.skipTest('misc owns OPC UA')
        leaves = ('nodeids.h', 'types_generated.h', 'types_generated.c',
                  'cpkt/opcua_types.h', 'cpkt/opcua_constants.h', 'cpkt/opcua_plugins.h',
                  'opcua_plugins_metadata.inc', 'opcua_types_metadata.inc',
                  'opcua_message_metadata.inc', 'opcua_codec_metadata.inc',
                  'opcua_eventloop_metadata.inc', 'opcua_config_metadata.inc',
                  'opcua_config_plugins_metadata.inc', 'opcua_nodestore_metadata.inc',
                  'opcua_native_format.c', 'opcua_native_nodestore.h',
                  'native/open62541/types_generated.c', 'native/open62541/types_generated.h',
                  'upstream', 'upstream/generate_datatypes.py')
        for leaf in leaves:
            for kind in ('leaf', 'dangling', 'directory', 'ancestor'):
                with self.subTest(leaf=leaf, kind=kind), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='generated-opcua-') as tmp:
                    repo, foreign, sentinel = self.seed(Path(tmp))
                    base = repo / 'build/generated/opcua'
                    link = self.redirect(base / leaf, kind, foreign, sentinel)
                    before = self.identity(sentinel)
                    result = self.operation(repo, [sys.executable, repo / 'tools/opcua/generate.py', '--upstream', repo / 'missing-sdk', '--output', base, '--fixture-table'])
                    self.assert_refused(result, link, foreign, sentinel, before)
                    if kind in ('leaf', 'dangling') and not leaf.startswith('upstream'):
                        self.assertFalse((base / 'upstream').exists())
                        self.assertFalse((base / 'nodeids.h').exists() and leaf != 'nodeids.h')

    def test_optional_pdf_and_public_api_preflight_all_outputs(self):
        if REPOSITORY_GROUP != 'misc':
            self.skipTest('misc owns PDF/OPC UA reports')
        for leaf in ('include/cpkt/pdf.h', 'src/pdf.c', 'cmake/exports/cpkt_pdf.txt', 'build/reports/owned/opcua.json'):
            for kind in ('leaf', 'dangling', 'directory', 'ancestor'):
                if leaf == 'src/pdf.c' and kind == 'ancestor':
                    # Its parent is covered; the next ancestor contains this
                    # generator's own script, whose ROOT resolves its identity.
                    continue
                with self.subTest(leaf=leaf, kind=kind), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='generated-optional-') as tmp:
                    repo, foreign, sentinel = self.seed(Path(tmp))
                    if 'opcua.json' in leaf:
                        owner = repo
                        output = owner / leaf
                        link = self.redirect(output, kind, foreign, sentinel)
                        command = [sys.executable, repo / 'tools/opcua/public_api.py', '--compiler', repo / 'missing-compiler', '--platform', 'linux', '--native-include', repo / 'native', '--facade-include', repo / 'facade', '--generated-include', repo / 'generated', '--contract', repo / 'contract', '--report', output]
                    else:
                        owner = repo / 'build/pdf-owner'
                        (owner / 'tools').mkdir(parents=True)
                        (owner / 'scripts').mkdir()
                        shutil.copy2(repo / 'tools/generate_pdf_facade.py', owner / 'tools')
                        shutil.copy2(repo / 'scripts/generated_output_paths.py', owner / 'scripts')
                        output = owner / leaf
                        link = self.redirect(output, kind, foreign, sentinel)
                        command = [sys.executable, owner / 'tools/generate_pdf_facade.py', repo / 'missing-sdk']
                    before = self.identity(sentinel)
                    result = self.operation(repo, command)
                    self.assert_refused(result, link, foreign, sentinel, before)
                    for sibling in ('include/cpkt/pdf.h', 'src/pdf.c', 'cmake/exports/cpkt_pdf.txt'):
                        if sibling != leaf and not any(parent.is_symlink() for parent in (owner / sibling).parents):
                            self.assertFalse((owner / sibling).exists())

    def test_optional_native_opcua_target_refuses_late_redirects(self):
        if REPOSITORY_GROUP != 'misc':
            self.skipTest('misc owns native OPC UA generation')
        for leaf in ('cpkt/opcua_constants.h', 'opcua_codec_metadata.inc', 'opcua_native_nodestore.h'):
            for kind in ('leaf', 'dangling', 'directory', 'ancestor'):
                with self.subTest(leaf=leaf, kind=kind), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='native-generated-opcua-') as tmp:
                    repo, foreign, sentinel = self.seed(Path(tmp))
                    graph = repo / 'build/graph'
                    text = (ROOT / 'CMakeLists.txt').read_text()
                    command = re.search(r'set\(CPKT_OPCUA_GENERATED_OUTPUTS .*?VERBATIM\)', text, re.S).group()
                    upstream = repo / 'tiny-sdk/open62541/install/share/open62541'
                    upstream.mkdir(parents=True)
                    (upstream / 'input.txt').write_text('immutable input\n')
                    (repo / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(probe NONE)\n'
                        'add_library(cpkt::open62541_static INTERFACE IMPORTED)\n'
                        'include(cmake/CpktMutationPaths.cmake)\n'
                        'set(CPKT_NATIVE_MAKE_PROGRAM \"' + shutil.which('ninja') + '\" CACHE FILEPATH \"\")\n'
                        'set(CMAKE_MAKE_PROGRAM \"${CMAKE_SOURCE_DIR}/scripts/native-build.sh\" CACHE FILEPATH \"\" FORCE)\n'
                        'set(Python3_EXECUTABLE "' + sys.executable + '")\n'
                        'set(CPKT_EXTERNAL_ROOT "' + str(repo / 'tiny-sdk') + '")\n'
                        'set(CPKT_OPCUA_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/opcua")\n' + command + '\ncpkt_write_generated_output_inventory()\n'
                        'add_custom_target(private_child COMMAND \"${CMAKE_COMMAND}\" -E touch \"${CMAKE_SOURCE_DIR}/child-marker\")\n'
                        'add_custom_target(facade DEPENDS "${CPKT_OPCUA_GENERATED_DIR}/cpkt/opcua_types.h")\nadd_dependencies(facade private_child)\n')
                    result = self.operation(repo, ['cmake', '-S', repo, '-B', graph, '-G', 'Ninja'])
                    self.assertEqual(0, result.returncode, result.stdout)
                    base = graph / 'generated/opcua'
                    link = self.redirect(base / leaf, kind, foreign, sentinel)
                    before = self.identity(sentinel)
                    result = self.operation(repo, ['cmake', '--build', graph, '--target', 'facade'])
                    self.assert_refused(result, link, foreign, sentinel, before)
                    self.assertFalse((repo / 'child-marker').exists())
                    self.assertFalse((base / 'upstream/input.txt').exists())
                    self.assertFalse((base / 'nodeids.h').exists())

    def test_optional_pdf_generation_header_export_parity(self):
        if REPOSITORY_GROUP != 'misc':
            self.skipTest('misc owns PDF generation')
        with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='generated-pdf-good-') as tmp:
            repo, foreign, sentinel = self.seed(Path(tmp))
            owner = repo / 'build/pdf-owner'
            for directory in ('tools', 'scripts', 'include/cpkt', 'src', 'cmake/exports'):
                (owner / directory).mkdir(parents=True, exist_ok=True)
            shutil.copy2(repo / 'tools/generate_pdf_facade.py', owner / 'tools')
            shutil.copy2(repo / 'scripts/generated_output_paths.py', owner / 'scripts')
            include = repo / 'tiny-sdk'
            include.mkdir()
            (include / 'hpdf.h').write_text(''.join('HPDF_EXPORT(int) HPDF_Probe' + str(index) + '(void);\n' for index in range(290)))
            (include / 'hpdf_types.h').write_text('/*  native OS integer types */\ntypedef int HPDF_INT;\n#ifdef __cplusplus\n')
            (include / 'hpdf_consts.h').write_text('#define  HPDF_TRUE 1\n#endif\n')
            alias = repo / 'immutable-sdk-link'
            alias.symlink_to(include, target_is_directory=True)
            command = [sys.executable, owner / 'tools/generate_pdf_facade.py', alias]
            result = self.operation(repo, command)
            self.assertEqual(0, result.returncode, result.stdout)
            catalog = (owner / 'cmake/exports/cpkt_pdf.txt').read_text()
            symbols = [line for line in catalog.splitlines() if line.startswith('cpkt_')]
            header = (owner / 'include/cpkt/pdf.h').read_text()
            source = (owner / 'src/pdf.c').read_text()
            self.assertEqual(290, len(symbols))
            self.assertEqual(sorted(symbols), symbols)
            for name in symbols:
                self.assertIn(name + '(void)', header)
                self.assertIn(name + '(void)', source)
            self.assertEqual([sentinel], list(foreign.iterdir()))

    def tiny_native_graph(self, repo, generator, *, producer=False, compiler=False):
        tool = self.native_tool(repo, generator)
        initial = self.operation(repo, ['cmake', '-E', 'env', 'CPKT_NATIVE_MAKE_PROGRAM=' + str(tool), repo / 'scripts/native-build.sh', '--version'])
        self.assertEqual(0, initial.returncode, initial.stdout)
        graph = repo / 'build/graph'
        text = 'cmake_minimum_required(VERSION 3.21)\n' + self.native_context(repo, tool, producer)
        text += 'project(probe ' + ('C' if compiler else 'NONE') + ')\n'
        if compiler:
            (repo / 'probe.c').write_text('int main(void) { return 0; }\n')
            text += ('if(NOT probe_round)\nset(probe_round 0)\nendif()\n'
                     'math(EXPR probe_round "${probe_round}+1")\nset(probe_round ${probe_round} CACHE STRING "" FORCE)\n'
                     'try_compile(result "${CMAKE_BINARY_DIR}/CMakeFiles/probe-${probe_round}" "${CMAKE_SOURCE_DIR}/probe.c")\n'
                     'if(NOT result)\nmessage(FATAL_ERROR "compiler probe failed")\nendif()\n'
                     'add_executable(probe probe.c)\n')
        text += 'add_custom_target(private_child COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_SOURCE_DIR}/child-marker")\n'
        if not producer:
            text += 'include(cmake/CpktMutationPaths.cmake)\ncpkt_write_generated_output_inventory()\n'
        (repo / 'CMakeLists.txt').write_text(text)
        configure = ['cmake', '-S', repo, '-B', graph, '-G', generator, '--debug-trycompile']
        result = self.operation(repo, configure)
        self.assertEqual(0, result.returncode, result.stdout)
        return graph, configure

    def test_configured_empty_consumer_inventory_required_before_build_and_native_clean(self):
        for generator in ('Ninja', 'Unix Makefiles'):
            with self.subTest(generator=generator), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='native-empty-consumer-') as tmp:
                repo, foreign, sentinel = self.seed(Path(tmp))
                graph, configure = self.tiny_native_graph(repo, generator)
                listing = graph / 'cpkt-generated-outputs.txt'
                self.assertEqual(b'', listing.read_bytes())
                result = self.operation(repo, ['cmake', '--build', graph, '--parallel', '2', '--target', 'private_child'])
                self.assertEqual(0, result.returncode, result.stdout)
                self.assertTrue((repo / 'child-marker').is_file())
                self.assertRegex((repo / 'native-calls').read_text(), r'(?:-j\s*2|--jobs=2)')
                (repo / 'child-marker').unlink()
                (repo / 'native-calls').unlink()
                evidence = self.native_evidence(repo)
                before = self.identity(sentinel)
                listing.unlink()
                routes = (['cmake', '--build', graph, '--target', 'private_child'],
                          [repo / 'scripts/native-build.sh', '-C', graph, 'clean'],
                          [repo / 'scripts/native-build.sh', '-C' + str(graph), 'private_child'],
                          [repo / 'scripts/native-build.sh', '-C', graph, '--version'])
                if generator == 'Unix Makefiles':
                    routes += ([repo / 'scripts/native-build.sh', '--directory=' + str(graph), 'clean'],
                               [repo / 'scripts/native-build.sh', '--directory', graph, 'private_child'])
                for command in routes:
                    with self.subTest(command=command):
                        result = self.operation(repo, command)
                        self.assertNotEqual(0, result.returncode, result.stdout)
                        self.assertIn('Required generated-output inventory is missing', result.stdout)
                        self.assertIn('scripts/build.sh configure', result.stdout)
                        self.assert_native_untouched(repo, evidence, foreign, sentinel, before)
                result = self.operation(repo, configure)
                self.assertEqual(0, result.returncode, result.stdout)
                self.assertEqual(b'', listing.read_bytes())
                result = self.operation(repo, [repo / 'scripts/native-build.sh', '-C', graph, 'clean'])
                self.assertEqual(0, result.returncode, result.stdout)
                for path, identity in evidence.items():
                    if path.parent.name == REPOSITORY_GROUP and not path.name.startswith('component-'):
                        self.assertFalse(path.exists())
                    else:
                        self.assertEqual(identity, self.identity(path))

    def test_native_producer_and_initial_compiler_probes_need_no_inventory(self):
        for generator in ('Ninja', 'Unix Makefiles'):
            for producer in (False, True):
                with self.subTest(generator=generator, producer=producer), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='native-inventory-exempt-') as tmp:
                    repo, foreign, sentinel = self.seed(Path(tmp))
                    graph, configure = self.tiny_native_graph(repo, generator, producer=producer, compiler=True)
                    # A second configure creates a fresh try_compile below an existing parent cache.
                    result = self.operation(repo, configure)
                    self.assertEqual(0, result.returncode, result.stdout)
                    probes = list((graph / 'CMakeFiles').rglob('CMakeCache.txt'))
                    self.assertTrue(probes)
                    self.assertTrue(all(not (path.parent / 'cpkt-generated-outputs.txt').exists() for path in probes))
                    calls = (repo / 'native-calls').read_text()
                    self.assertIn('--version', calls)
                    self.assertIn('cmTC_', calls)
                    self.assertEqual(producer, not (graph / 'cpkt-generated-outputs.txt').exists())
                    for _ in range(2):
                        result = self.operation(repo, ['cmake', '--build', graph, '--parallel', '2', '--target', 'probe', 'private_child'])
                        self.assertEqual(0, result.returncode, result.stdout)
                    self.assertTrue((repo / 'child-marker').exists())
                    result = self.run_command([graph / 'probe'], repo)
                    self.assertEqual(0, result.returncode, result.stdout)
                    evidence = self.native_evidence(repo)
                    result = self.operation(repo, [repo / 'scripts/native-build.sh', '-C', graph, 'clean'])
                    self.assertEqual(0, result.returncode, result.stdout)
                    for path, identity in evidence.items():
                        if path.parent.name == REPOSITORY_GROUP and (producer or not path.name.startswith('component-')):
                            self.assertFalse(path.exists())
                        else:
                            self.assertEqual(identity, self.identity(path))
                    self.assertFalse((graph / 'probe').exists())

    def test_native_inventory_malformed_or_nonregular_never_executes_or_revokes(self):
        for kind in ('directory', 'fifo', 'relative', 'traversal', 'blank'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='native-inventory-invalid-') as tmp:
                repo, foreign, sentinel = self.seed(Path(tmp))
                graph, _ = self.tiny_native_graph(repo, 'Ninja')
                listing = graph / 'cpkt-generated-outputs.txt'
                listing.unlink()
                if kind == 'directory':
                    listing.mkdir()
                elif kind == 'fifo':
                    os.mkfifo(listing)
                else:
                    listing.write_text('relative/output\n' if kind == 'relative' else str(graph / '../output') + '\n' if kind == 'traversal' else '\n')
                evidence = self.native_evidence(repo)
                before = self.identity(sentinel)
                (repo / 'native-calls').unlink(missing_ok=True)
                for command in (['cmake', '--build', graph, '--target', 'private_child'],
                                [repo / 'scripts/native-build.sh', '-C', graph, 'clean']):
                    result = self.operation(repo, command)
                    self.assertNotEqual(0, result.returncode, result.stdout)
                    self.assert_native_untouched(repo, evidence, foreign, sentinel, before)

    def test_native_output_inventory_refused_before_child_or_clean_removal(self):
        for leaf in ('inventory', 'product'):
            for kind in (('leaf', 'dangling') if leaf == 'inventory' else ('leaf', 'dangling', 'directory', 'ancestor')):
                with self.subTest(leaf=leaf, kind=kind), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='native-output-list-') as tmp:
                    repo, foreign, sentinel = self.seed(Path(tmp))
                    graph = repo / 'build/native/graph'
                    graph.mkdir(parents=True)
                    child = repo / 'native-child'
                    child.write_text('#!/bin/sh\necho child > "' + str(repo / 'child-marker') + '"\n')
                    child.chmod(0o755)
                    (graph / 'CMakeCache.txt').write_text('CPKT_NATIVE_MAKE_PROGRAM:FILEPATH=' + str(child) + '\nCPKT_TARGET_ID:STRING=fixture-target\nCPKT_GROUP:STRING=' + REPOSITORY_GROUP + '\n')
                    product = repo / 'build/generated/owned/product.h'
                    listing = graph / 'cpkt-generated-outputs.txt'
                    if leaf == 'inventory':
                        link = self.redirect(listing, kind, foreign, sentinel)
                    else:
                        listing.write_text(str(product) + '\n')
                        link = self.redirect(product, kind, foreign, sentinel)
                    ready = repo / 'build/verification/fixture-target' / REPOSITORY_GROUP / 'Debug-development.json'
                    ready.parent.mkdir(parents=True)
                    ready.write_text('existing Ready\n')
                    ready.chmod(0o600)
                    before_ready = self.identity(ready)
                    # A redirected graph with no cache uses the documented
                    # compiler-probe native-tool environment fallback.
                    driver = repo / 'native-driver.sh'
                    driver.write_text('#!/bin/sh\ncd "' + str(graph) + '"\nexport CPKT_NATIVE_MAKE_PROGRAM="' + str(child) + '"\nexec bash "' + str(repo / 'scripts/native-build.sh') + '" clean\n')
                    before = self.identity(sentinel)
                    result = self.operation(repo, ['bash', driver])
                    self.assert_refused(result, link, foreign, sentinel, before)
                    self.assertEqual(before_ready, self.identity(ready))
                    self.assertFalse((repo / 'child-marker').exists())

    def test_core_auth_generator_families_preflight_before_children(self):
        if REPOSITORY_GROUP != 'core':
            self.skipTest('core owns authentication generator probes')
        leaves = ('native-gssapi', 'native-sasl', 'facade', 'typed-bindings.c',
                  'typed-bindings-fixture.o', 'constant-bindings-gssapi.c',
                  'constant-bindings-sasl.c', 'constants-gssapi-fixture.o',
                  'constants-sasl-fixture.o', 'native-batch', 'signature-batch')
        for leaf in leaves:
            for kind in ('leaf', 'dangling', 'directory', 'ancestor'):
                with self.subTest(leaf=leaf, kind=kind), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='generated-auth-') as tmp:
                    repo, foreign, sentinel = self.seed(Path(tmp))
                    base = repo / 'build/auth/owned'
                    name = leaf.removeprefix('native-') + '.i' if leaf.startswith('native-') else 'facade.i' if leaf in ('facade', 'signature-batch') else leaf
                    if leaf == 'native-batch':
                        name = 'sasl.i'
                    link = self.redirect(base / name, kind, foreign, sentinel)
                    script = repo / 'auth-probe.py'
                    invoke = ('import auth_api_contract as m\nm.output_dir=lambda target:Path(' + repr(str(base.parent)) + ')\nm.check(["owned"])\n' if leaf == 'native-batch' else
                              'import auth_facade_signatures as m\nm.native_contract.output_dir=lambda target:Path(' + repr(str(base.parent)) + ')\nm.check(["owned"])\n' if leaf == 'signature-batch' else
                              'import auth_api_contract as m\nm.output_dir=lambda target:Path(' + repr(str(base.parent)) + ')\nm.compiler=child\nm.inspect("owned",' + repr(leaf.removeprefix('native-')) + ')\n' if leaf.startswith('native-') else
                              'import auth_facade_signatures as m\nm.native_contract.output_dir=lambda target:Path(' + repr(str(base.parent)) + ')\nm.native_contract.compiler=child\nm.inspect("owned")\n' if leaf == 'facade' else
                              'import auth_facade_bindings as m\nm.scratch_dir=lambda *args:Path(' + repr(str(base)) + ')\nm.compiler=child\nm.check(["fixture"])\n')
                    script.write_text('import sys\nfrom pathlib import Path\nsys.path.insert(0,' + repr(str(repo / 'scripts')) + ')\n'
                        'def child(*args):\n Path(' + repr(str(repo / 'child-marker')) + ').write_text("child")\n raise RuntimeError("unexpected child")\n' + invoke)
                    sibling = None
                    if kind in ('leaf', 'dangling') and name != 'typed-bindings.c':
                        sibling = base / 'typed-bindings.c'
                        sibling.write_text('existing sibling bytes\n')
                        sibling.chmod(0o600)
                        sibling_before = self.identity(sibling)
                    before = self.identity(sentinel)
                    result = self.operation(repo, [sys.executable, script])
                    self.assert_refused(result, link, foreign, sentinel, before)
                    self.assertFalse((repo / 'child-marker').exists())
                    if sibling:
                        self.assertEqual(sibling_before, self.identity(sibling))

    def test_db_contract_batch_refuses_late_output_before_evidence(self):
        if REPOSITORY_GROUP != 'db':
            self.skipTest('db owns this contract batch')
        for kind in ('leaf', 'dangling', 'directory', 'ancestor'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='generated-db-batch-') as tmp:
                repo, foreign, sentinel = self.seed(Path(tmp))
                base = repo / 'build/probes'
                output = base / 'contract/owned/sqlite-native.o'
                link = self.redirect(output, kind, foreign, sentinel)
                script = repo / 'db-batch.py'
                script.write_text('import sys\nfrom pathlib import Path\nsys.path.insert(0,' + repr(str(repo / 'scripts')) + ')\n'
                    'import db_api_contract as m\nm.inventory.output_dir=lambda target:Path(' + repr(str(base / 'inventory')) + ')\n'
                    'def child(*args):\n Path(' + repr(str(repo / 'child-marker')) + ').write_text("child")\n raise RuntimeError("unexpected child")\n'
                    'm.inventory.inspect=child\nm.check("owned")\n')
                before = self.identity(sentinel)
                result = self.operation(repo, [sys.executable, script])
                self.assert_refused(result, link, foreign, sentinel, before)
                self.assertFalse((base / 'inventory').exists())
                self.assertFalse((repo / 'child-marker').exists())

    def configured_fixture(self, work):
        repo, foreign, sentinel = self.seed(work)
        tools = repo / 'build/test-tools'
        tools.mkdir(parents=True)
        marker = repo / 'build/child-marker'
        tool = tools / 'compiler'
        tool.write_text('#!' + sys.executable + '\nimport json,sys\nfrom pathlib import Path\n'
            'Path(' + repr(str(marker)) + ').write_text("child")\n'
            'if "-c" in sys.argv: Path(sys.argv[sys.argv.index("-o")+1]).write_bytes(b"object")\n'
            'elif "-ast-dump=json" in sys.argv: print(json.dumps({"inner":[]}))\n'
            'elif "-fdump-record-layouts-complete" not in sys.argv: print("#define SQLITE_OK 0")\n')
        tool.chmod(0o755)
        (tools / 'clang').symlink_to(tool)
        (tools / 'compiler-alias').symlink_to(tool)
        inputs = repo / 'build/native-input'
        inputs.mkdir()
        (repo / 'build/input-alias').symlink_to(inputs, target_is_directory=True)
        from native_lifecycle_fixture import environment
        env = dict(environment(), CPKT_CONFIGURED_GROUP=REPOSITORY_GROUP,
                   PATH=str(tools) + os.pathsep + os.environ['PATH'])
        return repo, foreign, sentinel, env

    def configured_cache(self, repo, directory, target='x86_64-linux-gnu', group=None):
        directory.mkdir(parents=True, exist_ok=True)
        (directory / 'CMakeCache.txt').write_text('CPKT_TARGET_ID:STRING=' + target + '\n'
            'CPKT_GROUP:STRING=' + (group or REPOSITORY_GROUP) + '\n'
            'CMAKE_C_COMPILER:FILEPATH=' + str(repo / 'build/test-tools/compiler-alias') + '\n')

    def configured_command(self, repo, env, action):
        target = 'x86_64-linux-gnu'
        if action == 'inventory':
            command = [sys.executable, repo / 'scripts/db_api_inventory.py',
                       '--target', target, '--provider', 'sqlite']
        else:
            driver = repo / 'build/configured-probe.py'
            expression = ('m.compile_probe(' + repr(target) + ', "sqlite", ["int probe(void) { return 1; }"], native=True, native_include=Path(' + repr(str(repo / 'build/input-alias')) + '))'
                          if action == 'compile' else 'm.check(' + repr(target) + ')' if action == 'batch' else
                          'm.inspect(' + repr(target) + ', "sasl", include_override=Path(' + repr(str(repo / 'build/input-alias')) + '))')
            module = 'auth_api_contract' if action == 'auth' else 'db_api_contract'
            driver.write_text('import sys\nfrom pathlib import Path\nsys.path.insert(0,' + repr(str(repo / 'scripts')) + ')\n'
                              'import ' + module + ' as m\n' + expression + '\n')
            command = [sys.executable, driver]
        return subprocess.run(list(map(str, command)), cwd=repo, env=env, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def foreign_tree(self, foreign):
        return {str(path.relative_to(foreign)): (path.lstat().st_mode, path.lstat().st_ino,
                path.lstat().st_mtime_ns, path.read_bytes() if path.is_file() else None)
                for path in foreign.rglob('*')}

    def test_configured_output_directory_lexical_ancestry_before_public_probes(self):
        actions = ['inventory'] + (['compile', 'batch'] if REPOSITORY_GROUP == 'db' else
                                  ['auth'] if REPOSITORY_GROUP == 'core' else [])
        for selection in ('selected', 'relative', 'default'):
            for kind in ('directory', 'ancestor', 'dangling'):
                for action in actions:
                    with self.subTest(selection=selection, kind=kind, action=action), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='configured-redirect-') as tmp:
                        repo, foreign, sentinel, env = self.configured_fixture(Path(tmp))
                        directory = repo / 'build/x86_64-linux-gnu' / REPOSITORY_GROUP / 'Debug'
                        link = directory.parent if kind == 'ancestor' else directory
                        destination = foreign / 'absent' if kind == 'dangling' else foreign
                        resolved = destination / directory.name if kind == 'ancestor' else destination
                        if kind != 'dangling':
                            self.configured_cache(repo, resolved)
                            old_output = resolved / 'db-completion/inventory/x86_64-linux-gnu/native-sqlite.i'
                            old_output.parent.mkdir(parents=True)
                            old_output.write_bytes(b'unrelated native-sqlite.i\n')
                            old_output.chmod(0o640)
                        link.parent.mkdir(parents=True, exist_ok=True)
                        link.symlink_to(destination, target_is_directory=True)
                        if selection != 'default':
                            env['CPKT_CONFIGURED_BINARY_DIR'] = str(directory if selection == 'selected' else directory.relative_to(repo))
                        before = self.foreign_tree(foreign)
                        result = self.configured_command(repo, env, action)
                        self.assertNotEqual(0, result.returncode, result.stdout)
                        self.assertIn('symlink ancestor', result.stdout.lower())
                        self.assertIn(str(link), result.stdout)
                        self.assertTrue(link.is_symlink())
                        self.assertEqual(before, self.foreign_tree(foreign))
                        self.assertFalse((repo / 'build/child-marker').exists())
                        type(self).redirects += 1

    def test_configured_downstream_outputs_preflight_with_real_adapter(self):
        roles = [('inventory', 'inventory/x86_64-linux-gnu/native-sqlite.i'),
                 ('inventory', 'inventory/x86_64-linux-gnu/native-sqlite.json')]
        if REPOSITORY_GROUP == 'db':
            roles += [('compile', 'contract/x86_64-linux-gnu/sqlite-native.c'),
                      ('compile', 'contract/x86_64-linux-gnu/sqlite-native.o'),
                      ('batch', 'contract/x86_64-linux-gnu/sqlite-native.o')]
        if REPOSITORY_GROUP == 'core':
            roles += [('auth', 'auth-completion/contract/x86_64-linux-gnu/sasl.i')]
        for action, leaf in roles:
            for kind in ('leaf', 'dangling', 'directory', 'ancestor'):
                with self.subTest(action=action, leaf=leaf, kind=kind), tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='configured-downstream-') as tmp:
                    repo, foreign, sentinel, env = self.configured_fixture(Path(tmp))
                    directory = repo / 'build/configured'
                    self.configured_cache(repo, directory)
                    env['CPKT_CONFIGURED_BINARY_DIR'] = str(directory)
                    output = directory / leaf if action == 'auth' else directory / 'db-completion' / leaf
                    link = self.redirect(output, kind, foreign, sentinel)
                    before = self.identity(sentinel)
                    result = self.configured_command(repo, env, action)
                    self.assert_refused(result, link, foreign, sentinel, before)
                    self.assertFalse((repo / 'build/child-marker').exists())
                    if kind in ('leaf', 'dangling'):
                        self.assertEqual([link], list(output.parent.iterdir()))
                    if action == 'batch':
                        self.assertFalse((directory / 'db-completion/inventory').exists())

    def test_configured_selection_cache_errors_and_immutable_aliases(self):
        from configured_build import binary_dir, cache_value, scratch_dir
        with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='configured-selection-') as tmp:
            repo, foreign, sentinel, env = self.configured_fixture(Path(tmp))
            target = 'x86_64-linux-gnu'
            debug = repo / 'build' / target / REPOSITORY_GROUP / 'Debug'
            release = debug.parent / 'Release'
            external = Path(tmp) / 'caller-owned/configured'
            for directory in (debug, release, external):
                self.configured_cache(repo, directory)
            env.pop('CPKT_CONFIGURED_GROUP')
            selections = [({}, debug), ({'PRESET':'release'}, release),
                          ({'CPKT_PRESET':'release'}, release),
                          ({'PRESET':'debug', 'CPKT_PRESET':'release'}, debug),
                          ({'GROUP':REPOSITORY_GROUP}, debug),
                          ({'GROUP':'wrong', 'CPKT_CONFIGURED_GROUP':REPOSITORY_GROUP}, debug),
                          ({'CPKT_CONFIGURED_BINARY_DIR':str(external), 'PRESET':'invalid'}, external)]
            for selectors, expected in selections:
                with self.subTest(selectors=selectors), patch.dict(os.environ, dict(env, **selectors), clear=True):
                    self.assertEqual(expected, binary_dir(repo, target))
                    self.assertEqual(expected / 'probe', scratch_dir(repo, target, 'probe'))
                    self.assertEqual(str(repo / 'build/test-tools/compiler-alias'), cache_value(repo, target, 'CMAKE_C_COMPILER'))
            with patch.dict(os.environ, dict(env, CPKT_CONFIGURED_BINARY_DIR=str(external)), clear=True):
                cache = external / 'CMakeCache.txt'
                cache.write_text(cache.read_text().replace('compiler-alias', 'compiler'))
                self.assertEqual(str(repo / 'build/test-tools/compiler'), cache_value(repo, target, 'CMAKE_C_COMPILER'))
                self.configured_cache(repo, external, target='aarch64-linux-gnu')
                with self.assertRaisesRegex(RuntimeError, 'configured target does not match'):
                    binary_dir(repo, target)
                self.configured_cache(repo, external, group=next(group for group in ('core','db','misc') if group != REPOSITORY_GROUP))
                with self.assertRaisesRegex(RuntimeError, 'configured group does not match'):
                    binary_dir(repo, target)
                cache.unlink()
                with self.assertRaisesRegex(RuntimeError, 'configured CMake cache missing'):
                    binary_dir(repo, target)
            for selectors, message in (({'GROUP':'unknown'}, 'unknown GROUP'),
                    ({'PRESET':'aarch64-linux-gnu-release'}, 'resolves to'),
                                        ({'CPKT_CONFIGURED_BINARY_DIR':str(external), 'PRESET':'release'}, 'configured CMake cache missing')):
                with self.subTest(selectors=selectors), patch.dict(os.environ, dict(env, **selectors), clear=True):
                    with self.assertRaisesRegex(RuntimeError, message):
                        binary_dir(repo, target)
            with patch.dict(os.environ, dict(env, PRESET='invalid'), clear=True):
                with self.assertRaises(subprocess.CalledProcessError):
                    binary_dir(repo, target)
            (debug / 'CMakeCache.txt').unlink()
            with patch.dict(os.environ, env, clear=True):
                with self.assertRaisesRegex(RuntimeError, 'configured CMake cache missing'):
                    binary_dir(repo, target)
            self.configured_cache(repo, debug)
            for selected in (debug, external):
                self.configured_cache(repo, selected)
                good_env = dict(env, CPKT_CONFIGURED_BINARY_DIR=str(selected))
                result = self.configured_command(repo, good_env, 'inventory')
                self.assertEqual(0, result.returncode, result.stdout)
                report = selected / 'db-completion/inventory' / target / 'native-sqlite.json'
                import json
                self.assertEqual({'SQLITE_OK':'0'}, json.loads(report.read_text())['macros'])
                self.assertTrue((repo / 'build/child-marker').is_file())
                (repo / 'build/child-marker').unlink()
                if REPOSITORY_GROUP == 'db':
                    result = self.configured_command(repo, good_env, 'compile')
                    self.assertEqual(0, result.returncode, result.stdout)
                    self.assertEqual(b'object', (selected / 'db-completion/contract' / target / 'sqlite-native.o').read_bytes())
                    (repo / 'build/child-marker').unlink()
            self.assertEqual([sentinel], list(foreign.iterdir()))

    @classmethod
    def tearDownClass(cls):
        print('Generated output redirects refused with bytes/modes preserved:', cls.redirects, file=sys.stderr)


if __name__ == '__main__':
    unittest.main()
