#!/usr/bin/env python3
"""Exact binary scope, stale/partial/corrupt payloads and absent source proof."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from cpkt_packages import artifacts,sha,check_snapshot
class Manifest(unittest.TestCase):
 def test_complete_inventory_and_all_original_negatives(self):
  with tempfile.TemporaryDirectory(dir=ROOT/'build') as temporary:
   base=Path(temporary);manifest=base/'CHECKSUMS';names=artifacts('1.2.3','binary')
   for name in names:(base/name).write_bytes(name.encode())
   valid=''.join(sha(base/name)+'  '+name+'\n' for name in names);manifest.write_text(valid)
   command=['bash',str(ROOT/'scripts/verify-dist-manifest.sh'),str(base),'cpkt','1.2.3','--scope','binary','--manifest',str(manifest)]
   def run(passed):
    result=subprocess.run(command,capture_output=True,text=True)
    self.assertEqual(result.returncode==0,passed,result.stdout+result.stderr)
   run(True)
   for name in ('cpkt-1.2.3-extra-smoke-test.zip','cpkt-1.2.2-core-x86_64-linux-gnu.tar.gz','cpkt-1.2.2-CHECKSUMS','SHA256SUMS','arbitrary.txt'):
    (base/name).touch();run(False);(base/name).unlink()
   for name in names:
    payload=(base/name).read_bytes();(base/name).write_bytes(b'tamper');run(False);(base/name).write_bytes(payload)
   for invalid in (valid+valid, 'malformed  file\n','', valid.splitlines()[0]+'\n',valid+'a'*64+'  ../outside.tar.gz\n',valid+'a'*64+'  arbitrary.txt\n'):
    manifest.write_text(invalid);run(False)
   manifest.write_text(valid)
   old_source=base/'cpkt-1.2.3.tar.gz';old_source.write_bytes(b'old unverified source')
   run(True) # Binary success never certifies this old source.
   release=base/'release-CHECKSUMS';release.write_text(valid+sha(old_source)+'  '+old_source.name+'\n')
   with self.assertRaises((RuntimeError,ValueError,OSError)):check_snapshot(release,base,'1.2.3','release')
if __name__=='__main__':unittest.main()
