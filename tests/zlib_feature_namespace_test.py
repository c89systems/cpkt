#!/usr/bin/env python3
"""Installed zconf features must not pollute a consumer's private config macros."""
from pathlib import Path
import subprocess
import sys
import tempfile

if not __debug__:
    raise SystemExit("Feature-namespace tests require Python assertions enabled")

repo, build_root = (Path(p).resolve() for p in sys.argv[1:3])
compiler = sys.argv[3]
with tempfile.TemporaryDirectory(prefix="zconf-namespace-", dir=build_root) as work:
    root = Path(work)
    template = '''#if HAVE_UNISTD_H-0     /* may be set to #if 1 by ./configure */
#  define Z_HAVE_UNISTD_H
#endif
#if HAVE_STDARG_H-0     /* may be set to #if 1 by ./configure */
#  define Z_HAVE_STDARG_H
#endif
'''
    for name in ("zconf.h", "zconf.h.in"):
        (root / name).write_text(template)
    (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(zconf_namespace NONE)
set(zlib_BINARY_DIR "${CMAKE_BINARY_DIR}")
file(WRITE "${zlib_BINARY_DIR}/zconf.h.cmakein" "")
file(APPEND "${zlib_BINARY_DIR}/zconf.h.cmakein" "#cmakedefine HAVE_STDARG_H 1\\n")
file(APPEND "${zlib_BINARY_DIR}/zconf.h.cmakein" "#cmakedefine HAVE_UNISTD_H 1\\n")
file(READ "${CMAKE_SOURCE_DIR}/zconf.h" zconf_template)
file(APPEND "${zlib_BINARY_DIR}/zconf.h.cmakein" "${zconf_template}")
set(HAVE_STDARG_H TRUE)
set(HAVE_UNISTD_H TRUE)
configure_file(${zlib_BINARY_DIR}/zconf.h.cmakein ${zlib_BINARY_DIR}/zconf.h)
# Existing single-pass marker: exercise header patching even on a patched tree.
# add_library(zlib_object OBJECT
''')
    before = root / "before"
    subprocess.run(["cmake", "-S", str(root), "-B", str(before)], check=True)
    negative = root / "before.c"
    negative.write_text('#define HAVE_UNISTD_H\n#define HAVE_STDARG_H\n#include <zconf.h>\nint value(void) { return 0; }\n')
    result = subprocess.run([compiler, "-std=c89", "-Wall", "-Wextra", "-Werror", "-I", str(before),
                             "-c", str(negative), "-o", str(root / "before.o")], capture_output=True, text=True)
    assert result.returncode and "redefined" in result.stderr
    for _ in range(2):
        subprocess.run(["cmake", f"-DCPKT_ZLIB_SOURCE_DIR={root}", "-P", str(repo / "cmake/patch_zlib_single_pass.cmake")], check=True)
    build = root / "build"
    subprocess.run(["cmake", "-S", str(root), "-B", str(build)], check=True)
    for value in (None, "", "0", "1"):
        source = root / "consumer.c"
        definitions = "" if value is None else f"#define HAVE_UNISTD_H {value}\n#define HAVE_STDARG_H {value}\n"
        assertions = '''#ifndef Z_HAVE_UNISTD_H
#error zlib lost its detected unistd feature
#endif
#ifndef Z_HAVE_STDARG_H
#error zlib lost its detected stdarg feature
#endif
'''
        if value is None:
            assertions += '''#if defined(HAVE_UNISTD_H) || defined(HAVE_STDARG_H)
#error zlib leaked private feature macros
#endif
'''
        elif value:
            assertions += f"#if HAVE_UNISTD_H != {value} || HAVE_STDARG_H != {value}\n#error zlib changed caller macros\n#endif\n"
        source.write_text(definitions + '#include <zconf.h>\n' + assertions + 'int value(void) { return 0; }\n')
        subprocess.run([compiler, "-std=c89", "-Wall", "-Wextra", "-Wundef", "-Werror", "-I", str(build),
                        "-c", str(source), "-o", str(root / "consumer.o")], check=True)
print("zconf retains detected features and preserves absent, empty, zero and one caller macros")
