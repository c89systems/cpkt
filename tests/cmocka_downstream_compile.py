#!/usr/bin/env python3
"""Compile the real downstream C89 behavior consumer with the selected compiler."""
from pathlib import Path
import subprocess
import re
import sys

compiler, source, generated, native, output = sys.argv[1:]
for header in (Path(generated)/'cpkt/cmocka.h',Path(generated)/'cpkt/cmocka_types.h'):
    text = header.read_text()
    banned = r'\b(?:intmax_t|uintmax_t|u?int(?:8|16|32|64)_t|bool|inline|__func__|__FUNCTION__|__extension__|__attribute__|_Generic)\b|#\s*include\s*[<"](?:stdint|stdbool)\.h|#\s*define[^\n]*\([^\n)]*\.\.\.'
    if re.search(banned,text):sys.exit('non-C89 public facade token in '+str(header))
result = subprocess.run([compiler, '-std=c89', '-pedantic-errors', '-Wall', '-Wextra', '-Werror',
                         '-I' + generated, '-c', source, '-o', output])
sys.exit(result.returncode)
