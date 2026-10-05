#!/usr/bin/env sh
set -eu

if [ "${CPKT_SDK_PREFIX:-}" = "" ]; then
  printf 'CPKT_SDK_PREFIX is required and must point at an extracted cpkt SDK\n' >&2
  exit 2
fi

output=${1:-./cpkt_lua_runtime_c89_example}
# Remaining arguments are individual link flags, including paths with spaces.
if [ "$#" -gt 0 ]; then shift; fi
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# Parse pkg-config's shell quoting as data, without evaluating shell code.
exec python3 - "$script_dir" "$output" "$@" <<'PY'
import os
from pathlib import Path
import shlex
import subprocess
import sys

try:
    source = Path(sys.argv[1])
    output = Path(sys.argv[2])
    output.parent.mkdir(parents=True, exist_ok=True)
    cc = os.environ.get('CC', 'cc')
    pkg_config = os.environ.get('PKG_CONFIG', 'pkg-config')
    environment = dict(os.environ, PKG_CONFIG_PATH='',
                       PKG_CONFIG_LIBDIR=str(Path(os.environ['CPKT_SDK_PREFIX']) / 'lib/pkgconfig'))
    def flags(*arguments):
        text = subprocess.check_output([pkg_config, *arguments, 'cpkt-lua-runtime'],
                                       env=environment, text=True)
        return shlex.split(text)
    cflags = shlex.split(os.environ.get('CPKT_EXAMPLE_CFLAGS', '')) + flags('--cflags')
    libraries = flags('--static', '--libs')
    objects = []
    for name, standard in [('main', 'c89'), ('host_module', 'c99')]:
        obj = output.parent / ('cpkt_lua_runtime_c89_' + name + '.o')
        subprocess.run([cc, '-std=' + standard, '-Wall', '-Wextra', '-Wpedantic',
                        '-Werror', '-c', str(source / (name + '.c')), '-o', str(obj),
                        *cflags], check=True)
        objects.append(str(obj))
    subprocess.run([cc, *objects, '-o', str(output), *libraries, *sys.argv[3:]], check=True)
except (OSError, ValueError, subprocess.CalledProcessError) as error:
    print('Lua example build: ' + str(error), file=sys.stderr)
    raise SystemExit(1)
PY
