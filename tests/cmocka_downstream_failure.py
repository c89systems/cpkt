#!/usr/bin/env python3
"""An intentional assertion must report the original location and exit one."""
import subprocess
import sys

result = subprocess.run(sys.argv[1:], capture_output=True, text=True)
output = result.stdout + result.stderr
if result.returncode != 1 or 'intentional failure 37' not in output or 'cmocka_downstream_behavior.c:' not in output:
    raise SystemExit('cmocka failure contract changed: ' + str(result.returncode) + '\n' + output)
print('intentional cmocka assertion returned one with its diagnostic and source location')
