#!/usr/bin/env python3
"""Read the existing pinned AFL collection without provisioning any tools."""
import os
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
recipe = (root/'scripts/cpkt-aflpp.sh').read_text()
version = re.search(r'^version=([^\n]+)',recipe,re.M).group(1)
revision = re.search(r'^build_revision=([^\n]+)',recipe,re.M).group(1)
try:
    result = subprocess.run([str(root/'scripts/cpkt-toolchains.sh'),'discover','x86_64-linux-gnu'],check=True,capture_output=True,text=True)
    description = dict(line.split('=',1) for line in result.stdout.splitlines() if '=' in line)
    if description.get('status') != 'ready':
        raise RuntimeError('ordinary pinned compiler is not ready')
    collection = Path(description['root']).name
    cache = Path(os.environ.get('CPKT_TOOLCHAIN_CACHE',Path(os.environ.get('XDG_CACHE_HOME',Path.home()/'.cache'))/'cpkt/toolchains'))
    installed = cache/'roots'/('aflplusplus-'+version+'-x86_64-linux-gnu-'+collection)
    tools = {'afl_fuzz':installed/'bin/afl-fuzz','afl_showmap':installed/'bin/afl-showmap',
             'cc':installed/'bin/cpkt-afl-gcc','cxx':installed/'bin/cpkt-afl-g++'}
    internal = [installed/'bin'/name for name in ('afl-cc','afl-gcc-fast','afl-g++-fast')]
    if (not all(os.access(path,os.X_OK) for path in [*tools.values(),*internal])
            or not (installed/('.cpkt-aflpp-revision-'+revision+'-'+collection)).is_file()
            or not (installed/'lib/afl/afl-gcc-pass.so').is_file()
            or not (installed/'lib/afl/afl-compiler-rt.o').is_file()):
        raise RuntimeError('pinned AFL collection unavailable; explicitly prepare scripts/cpkt-aflpp.sh ensure')
    print('status=ready\nsource=aflplusplus\nversion='+version+'\nroot='+str(installed)+'\nhelper='+str(installed/'lib/afl'))
    for name,path in tools.items():print(name+'='+str(path))
except (RuntimeError,OSError,subprocess.CalledProcessError,KeyError) as error:
    sys.exit(str(error))
