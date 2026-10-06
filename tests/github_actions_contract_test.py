#!/usr/bin/env python3
"""Native source workflow identity, bootstrap and diagnostic-only contracts."""
from pathlib import Path
import re
import subprocess
import os
import shlex
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from host_bash_prerequisite_test import HostBash

class Workflow(unittest.TestCase):
    def test_each_native_job_provisions_its_own_lock_tool(self):
        text=(ROOT/'.github/workflows/darwin-bundle.yml').read_text()
        for job in ('arm64',):
            with self.subTest(job=job):
                body=text.split('\n  '+job+':\n',1)[1]
                body=re.split(r'\n  [A-Za-z0-9_-]+:\n',body,maxsplit=1)[0]
                installs=re.findall(r'^\s+run:\s+(brew install[^\n]+)',body,re.M)
                packages={argument for command in installs for argument in shlex.split(command)[2:]}
                self.assertIn('util-linux',packages,job+' lacks its own flock prerequisite')
                self.assertIn('bash',packages,job+' lacks its own host Bash prerequisite')
                self.assertLess(body.index('brew install'),body.index('name: Select host Bash'))
                self.assertLess(body.index('name: Select host Bash'),body.index('name: Host Bash regression'))
                for command in ('make test-darwin-native',):
                    if command in body:
                        self.assertLess(body.index('name: Host Bash regression'),body.index(command))
                self.assertIn('shell: /bin/bash --noprofile --norc -e -o pipefail {0}',body)

    def test_source_checkout_identity_is_verified_before_native_execution(self):
        text=(ROOT/'.github/workflows/darwin-bundle.yml').read_text()
        match=re.search(r'      - name: Native source and runtime contract\n        run: \|\n((?:          [^\n]*\n)+)',text)
        self.assertIsNotNone(match)
        command='\n'.join(line[10:] for line in match.group(1).splitlines())
        mocks='''git() { test "$*" = "rev-parse HEAD"; printf '%s\\n' "$CPKT_TEST_CHECKED_OUT_SHA"; }
make() { test "$*" = "test-darwin-native"; printf 'native-verified\\n'; }
'''
        for actual,accepted in (('a'*40,True),('b'*40,False)):
            with self.subTest(accepted=accepted):
                env=dict(os.environ,GITHUB_SHA='a'*40,CPKT_TEST_CHECKED_OUT_SHA=actual)
                result=subprocess.run(['bash','-euc',mocks+command],cwd=ROOT,env=env,capture_output=True,text=True)
                self.assertEqual(result.returncode==0,accepted,result.stderr)
                self.assertEqual(result.stdout,'native-verified\n' if accepted else '')

    def test_workflow_has_no_release_or_draft_handoff_lane(self):
        text=(ROOT/'.github/workflows/darwin-bundle.yml').read_text()
        self.assertEqual(re.findall(r'^  ([A-Za-z0-9_-]+):$',text.split('\njobs:\n',1)[1],re.M),['arm64'])
        dispatch=text.split('  workflow_dispatch:',1)[1].split('\npermissions:',1)[0]
        self.assertEqual(dispatch.strip(),'')
        self.assertIn('ref: ${{ github.sha }}',text)
        self.assertIn('contents: read',text)
        for forbidden in ('inputs.','secrets.','CPKT_DRAFT_TOKEN','CPKT_HANDOFF','producer_commit',
                          'arm64-artifact','cpkt_github_handoff','make test-darwin-sdk',
                          'make release','gh release','git push'):
            self.assertNotIn(forbidden,text)

    def test_exact_native_routes_and_pins(self):
        text=(ROOT/'.github/workflows/darwin-bundle.yml').read_text()
        for value in ('workflow_dispatch:', 'branches: ["feature/**", "fix/**", trunk]', 'pull_request:', '  arm64:', 'make test-darwin-native', 'test "$(git rev-parse HEAD)" = "${GITHUB_SHA}"', 'persist-credentials: false'):
            self.assertIn(value,text)
        self.assertEqual(text.count('runs-on: macos-26'),1)
        self.assertEqual(text.count('timeout-minutes: 120'),1)
        self.assertEqual(text.count('CPKT_DEPENDENCY_BUILD_JOBS: "2"'),1)
        self.assertEqual(text.count('MACOSX_DEPLOYMENT_TARGET: "15.0"'),1)
        pins=re.findall(r'uses: ([^\s]+)',text)
        self.assertEqual(pins,['actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1','actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a'])
        self.assertIn('if: always()',text)
        self.assertIn('dist/*-arm64-apple-darwin*.tar.gz',text)
        self.assertIn('build/arm64-apple-darwin/*/Release/cpkt-test-results.xml',text)
    def test_native_surfaces_and_actual_runtime(self):
        result=subprocess.run(['make','--no-print-directory','-n','test-darwin-native','test-github-actions-contracts'],cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        for action in ('test-darwin-native','test-github-actions-contracts'):self.assertIn('"'+action+'"',result.stdout)
        code=(ROOT/'scripts/cpkt_darwin.py').read_text()
        for value in ('codesign','rebuild','resign'):
            if value=='codesign':continue
            self.assertNotIn("command(['"+value,code)
        self.assertIn("sys.platform!='darwin'",code)
        self.assertIn("'cpkt_abi_smoke_shared','cpkt_abi_smoke_static'",code)
        native=(ROOT/'scripts/darwin.sh').read_text()
        self.assertIn('build.sh" test --group "$cpkt_owner" --preset "$preset"',native)
        self.assertIn('cpkt_darwin.py" source-evidence',native)

if __name__=='__main__':
    suite=unittest.TestSuite([unittest.defaultTestLoader.loadTestsFromTestCase(Workflow),unittest.defaultTestLoader.loadTestsFromTestCase(HostBash)])
    sys.exit(not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful())
