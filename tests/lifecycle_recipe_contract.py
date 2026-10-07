#!/usr/bin/env python3
"""Observe Bash lifecycle sequencing with isolated, recording command fixtures."""
from pathlib import Path
import subprocess
import sys
import tempfile
from native_lifecycle_fixture import seed, environment

ROOT=Path(__file__).resolve().parents[1]
selected=sys.argv[1]
expected=sys.argv[2].splitlines()
(ROOT/'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='recipe-contract-',dir=ROOT/'build') as directory:
    root=Path(directory)
    seed(root)
    (root/'scripts/lifecycle.sh').write_text('#!/usr/bin/env bash\nprintf "%s\\n" "$1" >> "$(dirname "$0")/../phases"\n')
    (root/'scripts/build.sh').write_text('#!/usr/bin/env bash\nprintf "%s\\n" "$1" >> "$(dirname "$0")/../phases"\n')
    (root/'scripts/package.sh').write_text('#!/usr/bin/env bash\nprintf "%s\\n" "$1" >> "$(dirname "$0")/../phases"\n')
    (root/'scripts/cpkt-aflpp.sh').write_text('#!/usr/bin/env bash\nprintf "afl-%s\\n" "$1" >> "$(dirname "$0")/../phases"\n')
    subprocess.run(['bash',str(root/'scripts/release.sh'),selected],cwd=root,check=True,env=environment())
    actual=(root/'phases').read_text().splitlines()
    if actual!=expected:
        sys.exit('recipe '+selected+' expected '+repr(expected)+' observed '+repr(actual))
    if 'afl-ensure' in expected:
        (root/'phases').unlink()
        (root/'scripts/cpkt-aflpp.sh').write_text('#!/usr/bin/env bash\nprintf "afl-%s\\n" "$1" >> "$(dirname "$0")/../phases"\nexit 73\n')
        failed=subprocess.run(['bash',str(root/'scripts/release.sh'),selected],cwd=root,env=environment())
        assert failed.returncode==73,failed.returncode
        actual=(root/'phases').read_text().splitlines()
        assert actual==expected[:expected.index('afl-ensure')+1],actual
print('serialized Bash lifecycle recipe passed: '+selected)
