#!/usr/bin/env python3
"""Compile the actual trace patch additions before building dependencies.

Minimal native type declarations isolate the prototype contract from Kerberos
configuration. The real GSSAPI builds separately enforce the same diagnostic
against complete upstream translation units and generated native headers.
"""

from pathlib import Path
import subprocess
import sys
import tempfile


def additions(patch, filename):
    hunks = []
    selected = False
    current = None
    for line in patch.splitlines():
        if line.startswith("+++ "):
            selected = line[4:] == "b/" + filename
            current = None
        elif selected and line.startswith("@@ "):
            current = []
            hunks.append(current)
        elif selected and current is not None and line.startswith("+"):
            current.append(line[1:])
    return ["\n".join(hunk) + "\n" for hunk in hunks]


def main():
    repo, build = (Path(p).resolve() for p in sys.argv[1:3])
    compiler = sys.argv[3]
    patch = (repo / "cmake/patches/krb5_gss_trace_callback.patch").read_text()
    base = "src/lib/gssapi/"
    declarations = "".join(additions(patch, base + "krb5/gssapi_krb5.h"))
    definitions = next(
        hunk for hunk in additions(patch, base + "krb5/init_sec_context.c")
        if "krb5_gss_apply_trace(krb5_context context)" in hunk
    )
    private = "".join(additions(patch, base + "generic/gssapiP_trace.h"))
    prototype = "krb5_error_code krb5_gss_apply_trace(krb5_context context);"
    # These are only the native types needed to compile the patch additions.
    # No mock dispatch, logging behavior, or substitute implementation is used.
    native = """#ifndef FIXTURE_KRB5_H
#define FIXTURE_KRB5_H
#define KRB5_CALLCONV
#define GSS_DLLIMP
#define NULL ((void *)0)
typedef int krb5_error_code;
typedef struct fixture_context *krb5_context;
typedef struct { const char *message; } krb5_trace_info;
krb5_error_code krb5_set_trace_callback(krb5_context,
    void (*)(krb5_context, const krb5_trace_info *, void *), void *);
#endif
"""
    with tempfile.TemporaryDirectory(prefix="krb5-trace-prototypes-", dir=build) as tmp:
        root = Path(tmp)
        (root / "krb5.h").write_text(native)
        header = root / "gssapiP_trace.h"
        source = root / "trace.c"
        source.write_text('#include "krb5.h"\n#include "gssapiP_trace.h"\n'
                          + declarations + definitions)
        command = [compiler, "-std=c89", "-Wall", "-Wextra", "-Werror",
                   "-Wmissing-prototypes", "-pedantic", "-c",
                   "-I", str(root), str(source), "-o", str(root / "trace.o")]

        def compile_case(expected=None):
            result = subprocess.run(command, capture_output=True, text=True)
            if expected is None:
                if result.returncode or result.stderr:
                    raise AssertionError("Trace patch is not warning-clean:\n" + result.stderr)
            elif not result.returncode or expected not in result.stderr:
                raise AssertionError("Compiler accepted broken trace declarations:\n"
                                     + result.stderr)

        header.write_text(private)
        compile_case()
        if prototype not in private:
            raise AssertionError("Missing private trace prototype")
        header.write_text(private.replace(prototype, ""))
        compile_case("krb5_gss_apply_trace")
        header.write_text(private.replace(prototype, prototype.replace("krb5_context", "int")))
        compile_case("krb5_gss_apply_trace")
        header.write_text(private)
        source.write_text(source.read_text().replace(
            "gss_krb5_set_trace_callback(gss_krb5_trace_callback callback, void *data);", ""))
        compile_case("gss_krb5_set_trace_callback")
    print("Trace patch compiles warning-clean; missing and conflicting prototypes are rejected")


if __name__ == "__main__":
    main()
