#!/usr/bin/env python3
"""Exact hosted lane identity and offline authenticated transport regressions."""
from pathlib import Path
import re
import subprocess
import json
import os
import shlex
import tempfile
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from package_integration_contract_test import Fixtures
from host_bash_prerequisite_test import HostBash

class Workflow(unittest.TestCase):
    def test_each_native_job_provisions_its_own_lock_tool(self):
        text=(ROOT/'.github/workflows/darwin-bundle.yml').read_text()
        for job in ('arm64','arm64-artifact'):
            with self.subTest(job=job):
                body=text.split('\n  '+job+':\n',1)[1]
                body=re.split(r'\n  [A-Za-z0-9_-]+:\n',body,maxsplit=1)[0]
                installs=re.findall(r'^\s+run:\s+(brew install[^\n]+)',body,re.M)
                packages={argument for command in installs for argument in shlex.split(command)[2:]}
                self.assertIn('util-linux',packages,job+' lacks its own flock prerequisite')
                self.assertIn('bash',packages,job+' lacks its own host Bash prerequisite')
                self.assertLess(body.index('brew install'),body.index('name: Select host Bash'))
                self.assertLess(body.index('name: Select host Bash'),body.index('name: Host Bash regression'))
                for command in ('make test-darwin-native','make test-darwin-sdk','preflight --handoff','download --handoff'):
                    if command in body:
                        self.assertLess(body.index('name: Host Bash regression'),body.index(command))
                self.assertIn('shell: /bin/bash --noprofile --norc -e -o pipefail {0}',body)

    def test_handoff_transport_preserves_duplicate_keys_for_strict_preflight(self):
        text=(ROOT/'.github/workflows/darwin-bundle.yml').read_text()
        command=next(line.strip() for line in text.splitlines() if "python3 -c" in line and 'CPKT_HANDOFF' in line)
        (ROOT/'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='handoff-transport-',dir=ROOT/'build') as directory:
            root=Path(directory);(root/'build/darwin-artifact-input').mkdir(parents=True)
            payload='{"schema_version":1,"schema_version":2}'
            result=subprocess.run(shlex.split(command),cwd=root,env=dict(os.environ,CPKT_HANDOFF=payload),capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            raw=(root/'build/darwin-artifact-input/handoff.json').read_bytes()
            self.assertEqual(raw.decode(),payload)
            sys.path.insert(0,str(ROOT/'scripts'))
            from cpkt_packages import validator
            with self.assertRaisesRegex(ValueError,'duplicate JSON key'):validator.decode(raw)
    def test_exact_native_routes_and_pins(self):
        text=(ROOT/'.github/workflows/darwin-bundle.yml').read_text()
        for value in ('workflow_dispatch:', 'branches: ["feature/**", "fix/**", trunk]', 'pull_request:', '  arm64:', '  arm64-artifact:', 'make test-darwin-native', 'make test-darwin-sdk', 'CPKT_DRAFT_TOKEN: ${{ secrets.CPKT_DRAFT_TOKEN }}', 'CPKT_EXPECTED_PRODUCER_COMMIT: ${{ inputs.producer_commit }}', 'test "${GITHUB_REF_TYPE}" = tag', 'preflight --handoff', 'download --handoff', 'persist-credentials: false'):
            self.assertIn(value,text)
        self.assertEqual(text.count('runs-on: macos-26'),2)
        self.assertEqual(text.count('timeout-minutes: 120'),2)
        self.assertEqual(text.count('CPKT_DEPENDENCY_BUILD_JOBS: "2"'),2)
        self.assertEqual(text.count('MACOSX_DEPLOYMENT_TARGET: "15.0"'),2)
        pins=re.findall(r'uses: ([^\s]+)',text)
        self.assertEqual(pins,['actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1','actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a']*2)
        artifact=text.split('  arm64-artifact:',1)[1]
        for forbidden in ('make release','make package','codesign -s','cmake --build','git push'):self.assertNotIn(forbidden,artifact)
        self.assertIn('if: always()',artifact)
        self.assertIn('dist/*-arm64-apple-darwin*.tar.gz',text)
        self.assertIn('build/arm64-apple-darwin/*/Release/cpkt-test-results.xml',text)
    def test_native_surfaces_and_actual_runtime(self):
        result=subprocess.run(['make','--no-print-directory','-n','test-darwin-native','test-darwin-sdk','test-github-actions-contracts'],cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        for action in ('test-darwin-native','test-darwin-sdk','test-github-actions-contracts'):self.assertIn('"'+action+'"',result.stdout)
        code=(ROOT/'scripts/cpkt_darwin.py').read_text()
        for value in ('codesign','rebuild','resign'):
            if value=='codesign':continue
            self.assertNotIn("command(['"+value,code)
        self.assertIn("sys.platform!='darwin'",code)
        self.assertIn("'cpkt_abi_smoke_shared','cpkt_abi_smoke_static'",code)
        native=(ROOT/'scripts/darwin.sh').read_text()
        self.assertIn('compose --group all --preset arm64-apple-darwin-native --base "$base" --fresh-owned',native)
        self.assertIn('codesign --verify --strict "$executable"',native)
        self.assertIn('"$executable"',native)

if __name__=='__main__':
    suite=unittest.TestSuite([unittest.defaultTestLoader.loadTestsFromTestCase(Workflow),unittest.defaultTestLoader.loadTestsFromTestCase(HostBash),unittest.TestSuite(Fixtures(name) for name in ('test_authenticated_draft_read_identity','test_digest_hit_zero_network_and_corrupt_miss','test_credentials_only_official_redirects'))])
    sys.exit(not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful())
