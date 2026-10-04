#!/usr/bin/env python3
"""Prove that auth contracts reject real declaration and constant mutations."""

import pathlib
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
import auth_api_contract as native  # noqa: E402
import auth_facade_signatures as facade  # noqa: E402

TARGET = "x86_64-linux-gnu"


def mutate(path, before, after):
    original = path.read_text()
    if original.count(before) != 1:
        raise RuntimeError("mutation fixture is not unique: " + before)
    path.write_text(original.replace(before, after))
    return original


def assert_rejected(gate, category):
    try:
        gate()
    except RuntimeError as error:
        if category not in str(error):
            raise RuntimeError("wrong rejection for " + category + ": " + str(error))
        return
    raise RuntimeError("contract accepted changed " + category)


def main():
    scratch = native.output_dir(TARGET).parent
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch, prefix="auth-mutation-") as tmp:
        include = pathlib.Path(tmp) / "include"
        shutil.copytree(ROOT / "include/cpkt", include / "cpkt")
        cases = (
            ("sasl_plugin.h", "void cpkt_sasl_md5_initialize(cpkt_sasl_md5_context *context);",
             "void cpkt_sasl_md5_initialize_REMOVED(cpkt_sasl_md5_context *context);",
             "functions"),
            ("sasl_plugin.h", "int cpkt_sasl_md5_update(cpkt_sasl_md5_context *context,\n                         const unsigned char *bytes, unsigned long length);",
             "int cpkt_sasl_md5_update(cpkt_sasl_md5_context *context,\n                         const void *bytes, unsigned long length);", "functions"),
            ("sasl.h", "unsigned long value_count;", "unsigned long missing_value_count;",
             "records"),
            ("sasl.h", "cpkt_sasl_simple_callback simple;", "int simple;",
             "records"),
            ("sasl.h", "typedef int (*cpkt_sasl_simple_callback)(void *context, int id,",
             "typedef int (*cpkt_sasl_simple_callback)(void *context, long id,",
             "typedefs"),
            ("sasl.h", "unsigned long value_count;\n  unsigned long total_value_bytes;",
             "unsigned long total_value_bytes;\n  unsigned long value_count;",
             "record_order"),
            ("gssapi.h", "  CPKT_GSS_OID_HOSTBASED_SERVICE_X,\n  CPKT_GSS_OID_COMPOSITE_EXPORT_NAME,",
             "  CPKT_GSS_OID_COMPOSITE_EXPORT_NAME,\n  CPKT_GSS_OID_HOSTBASED_SERVICE_X,",
             "enums"),
        )
        for file_name, before, after, category in cases:
            path = include / "cpkt" / file_name
            original = mutate(path, before, after)
            try:
                assert_rejected(lambda: facade.check([TARGET], include),
                                category)
            finally:
                path.write_text(original)
        native_include = pathlib.Path(tmp) / "native/include"
        staged = ROOT / ".cache/deps" / TARGET / "cyrus-sasl/install/include/sasl"
        shutil.copytree(staged, native_include / "sasl")
        native_cases = (
            ("sasl.h", "#define SASL_OK          0", "#define SASL_OK          7", "macros"),
            ("saslplug.h", "SASL_INFO_LIST_START = 0", "SASL_INFO_LIST_START = 7", "enums"),
            ("sasl.h", "sasl_client_init(const sasl_callback_t *callbacks)",
             "sasl_client_init(const void *callbacks)", "functions"),
            ("sasl.h", "    sasl_ssf_t min_ssf;\n    sasl_ssf_t max_ssf;",
             "    sasl_ssf_t max_ssf;\n    sasl_ssf_t min_ssf;",
             "record_order"),
        )
        for file_name, before, after, category in native_cases:
            path = native_include / "sasl" / file_name
            original = mutate(path, before, after)
            try:
                assert_rejected(
                    lambda: native.check([TARGET], {"sasl": native_include}),
                    category)
            finally:
                path.write_text(original)
    print("eleven auth declaration, callback, record and constant mutations rejected by gates")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
