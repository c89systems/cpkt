#!/usr/bin/env python3
"""Generate the complete pinned cmocka surface and its private native ABI bridge.

Scalar convenience operations use C89 long; every native integral operation also
has a words entrypoint, including callbacks and sets. Queue retrieval names the
function explicitly: ISO C89 has no current-function expression.
"""
import sys
import argparse
from pathlib import Path
import re


from generated_output_paths import validate_output_paths, write_generated_text


def active_source(source):
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    output, stack, active = [], [], True
    known = {'CMOCKA_H_': False, 'CMOCKA_NO_STANDARD_INCLUDES': False}
    lines = iter(source.splitlines())
    for line in lines:
        while line.rstrip().endswith('\\'):
            line = line.rstrip()[:-1] + ' ' + next(lines)
        match = re.match(r'\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)', line)
        if match:
            directive, expression = match.groups()
            expression = expression.strip()
            condition = known.get(expression, False)
            if directive == 'ifndef':
                condition = not condition
            if directive == 'if':
                condition = False  # compiler extensions and deprecated allocation redirects
            if directive in ('ifdef', 'ifndef', 'if'):
                stack.append((active, condition))
                active = active and condition
            elif directive == 'else':
                parent, prior = stack[-1]
                active = parent and not prior
                stack[-1] = (parent, True)
            elif directive == 'elif':
                active = False
            else:
                active, _ = stack.pop()
            continue
        if active:
            output.append(line)
            match = re.match(r'\s*#\s*define\s+(\w+)\s*$', line)
            if match:
                known[match[1]] = True
    return '\n'.join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--header', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    validate_output_paths(args.output, *(args.output.parent / name for name in
        ("cmocka_types.h", "cmocka_bridge.inc", "cmocka_bridge_exports.txt")))
    original = args.header.read_text()
    notice = re.match(r'\s*(/\*.*?\*/)', original, re.S).group(1)
    text = active_source(original)
    text = text.replace('#define CMOCKA_H_', '').replace('#include <stdbool.h>', '').replace('#include <stdint.h>', '')
    text = re.sub(r'\bbool\b', 'int', text)
    text = re.sub(r'static inline void _unit_test_dummy\(void \*\*state\)\s*\{.*?\}', 'void cpkt_cmocka_unit_test_dummy(void **state);\n#define _unit_test_dummy cpkt_cmocka_unit_test_dummy', text, flags=re.S)
    # Native declarations requiring width/union/callback conversion.
    prototypes = list(re.finditer(r'(?m)^(CMockaValueData|void|int)\s+(_\w+)\s*\((.*?)\)\s*(?:CMOCKA_DEPRECATED)?;', text, re.S))
    affected = [m for m in prototypes if re.search(r'intmax_t|uintmax_t|CMockaValueData|CheckParameter', m.group(0))]
    structs_start = text.index('typedef union {')
    structs_end = text.index('/*', structs_start) if False else text.index('void print_message', structs_start)
    types = text[structs_start:structs_end]
    # Remove preprocessor utility macros from the private type header.
    types = re.sub(r'(?m)^\s*#.*$', '', types)
    types = re.sub(r'\buintmax_t\b', 'unsigned long', types)
    types = re.sub(r'\bintmax_t\b', 'long', types)
    types = re.sub(r'\buint32_t\b', 'unsigned long', types)
    names = set(re.findall(r'(?:struct|enum)\s+(\w+)\s*\{', types))
    names.update(re.findall(r'}\s*(\w+)\s*;', types))
    names.update(re.findall(r'(?m)^typedef[^\n]*\(\*(\w+)\)', types))
    enum_values = set(re.findall(r'\b(?:UNIT_TEST_FUNCTION_TYPE_\w+|CM_OUTPUT_\w+)\b', types))
    # Private, disjoint types let the native header coexist in the bridge.
    mapping = {n: 'Cpkt' + n for n in names | enum_values}
    for native, public in sorted(mapping.items(), key=lambda x: -len(x[0])):
        types = re.sub(r'\b' + native + r'\b', public, types)
    types = re.sub(r',\s*}', '\n}', types)
    union_start = types.index('typedef union {')
    union_end = types.index('} CpktCMockaValueData;') + len('} CpktCMockaValueData;')
    types = types[:union_start] + '''typedef struct CpktCmockaWords { unsigned long high; unsigned long low; } CpktCmockaWords;
typedef struct CpktCMockaValueData {
    int kind;
    long int_val; unsigned long uint_val;
    float float_val; double real_val;
    void *ptr; const void *const_ptr; void *(*func)(void);
    CpktCmockaWords words;
} CpktCMockaValueData;''' + types[union_end:]
    declarations = ['CpktCMockaValueData cpkt_cmocka_value_int(long value);',
                    'CpktCMockaValueData cpkt_cmocka_value_uint(unsigned long value);',
                    'CpktCMockaValueData cpkt_cmocka_value_ptr(const void *value);',
                    'CpktCMockaValueData cpkt_cmocka_value_float(float value);',
                    'CpktCMockaValueData cpkt_cmocka_value_double(double value);',
                    'CpktCMockaValueData cpkt_cmocka_value_words(CpktCmockaWords value);',
                    'CpktCmockaWords cpkt_cmocka_words(unsigned long high, unsigned long low);',
                    'void cpkt_cmocka_location(const char *file, int line);',
                    'void cpkt_cmocka_fail_msg(const char *format, ...);']
    bridge, exports = [], []
    for match in affected:
        result, name, parameters = match.groups()
        if 'CheckParameter' in parameters:
            continue  # callback bridges implemented below
        params = [p.strip() for p in parameters.split(',')]
        for wide in (False, True):
            if wide and not re.search(r'intmax_t|uintmax_t', parameters):
                continue
            function = 'cpkt_cmocka' + name + ('_words' if wide else '')
            declarations_params, calls, before, cleanup = [], [], [], []
            for parameter in params:
                param_name = re.search(r'(\w+)\s*(?:\[\])?$', parameter).group(1)
                typ = 'uintmax_t' if 'uintmax_t' in parameter else 'intmax_t' if 'intmax_t' in parameter else None
                converted = parameter.replace('uintmax_t', 'CpktCmockaWords' if wide else 'unsigned long').replace('intmax_t', 'CpktCmockaWords' if wide else 'long').replace('int32_t', 'int').replace('CMockaValueData', 'CpktCMockaValueData')
                declarations_params.append(converted)
                if typ and '[]' in parameter:
                    count = 'number_of_values'
                    before += [f'{typ} *native_{param_name};', 'size_t i;',
                               f'native_{param_name} = cpkt_set_buffer_alloc({count}, sizeof(*native_{param_name}), file, (int)line);',
                               f'for (i = 0; i < {count}; ++i) native_{param_name}[i] = ' + (f'({typ})cpkt_words_native({param_name}[i]);' if wide else f'({typ}){param_name}[i];')]
                    calls.append('native_' + param_name)
                    cleanup.append('cpkt_set_buffer_free(native_' + param_name + ');')
                elif typ:
                    calls.append(f'({typ})cpkt_words_native({param_name})' if wide else f'({typ}){param_name}')
                elif 'CMockaValueData' in parameter:
                    calls.append('cpkt_value_native(' + param_name + ')')
                else:
                    calls.append(param_name)
            public_result = result.replace('CMockaValueData', 'CpktCMockaValueData')
            signature = public_result + ' ' + function + '(' + ', '.join(declarations_params) + ')'
            declarations.append(signature + ';')
            native_name = '_expect_uint_in_range' if name == '_expect_in_range' else name
            call = native_name + '(' + ', '.join(calls) + ')'
            action = ('return cpkt_value_public(' + call + ');' if result == 'CMockaValueData' else ('return ' if result != 'void' else '') + call + ';')
            if cleanup and result != 'void':
                raise RuntimeError('array bridge requires explicit result cleanup: ' + name)
            bridge.append(signature + ' {\n' + '\n'.join(before) + '\n' + action + '\n' + '\n'.join(cleanup) + '\n}')
            exports.append(function)
        text = re.sub(r'\b' + re.escape(name) + r'\b', 'cpkt_cmocka' + name, text)
    # Replace type block with the standalone private C89 definitions and aliases.
    start = text.index('typedef union {')
    end = text.index('void print_message', start)
    aliases = '\n'.join('#define ' + n + ' ' + p for n, p in sorted(mapping.items()))
    text = text[:start] + '#include "cmocka_types.h"\n' + aliases + '\n' + text[end:]
    for native, public in [('uintmax_t','unsigned long'),('intmax_t','long'),('uintptr_t','unsigned long'),('uint32_t','unsigned long'),('int32_t','int')]:
        text = re.sub(r'\b' + native + r'\b', public, text)
    # All queue operations require an explicit function token. No global state or
    # compiler intrinsic substitutes for the missing ISO C89 function name.
    macro_pattern = re.compile(r'(?m)^\s*#\s*define\s+(\w+)\(([^\n]*?)\)([^\n]*)')
    constructors = {'cast_int_to_cmocka_value':'uint', 'cast_ptr_to_cmocka_value':'ptr', 'assign_int_to_cmocka_value':'int', 'assign_uint_to_cmocka_value':'uint', 'assign_float_to_cmocka_value':'float', 'assign_double_to_cmocka_value':'double'}
    def rewrite(m):
        name, params, body = m.groups()
        if name in constructors:
            return '#define ' + name + '(value) cpkt_cmocka_value_' + constructors[name] + '(value)'
        if name == 'fail_msg':
            return '#define fail_msg (cpkt_cmocka_location(__FILE__, __LINE__), cpkt_cmocka_fail_msg)'
        if name == 'cm_print_error':
            return '#define cm_print_error cmocka_print_error'
        if '__func__' in body:
            params = 'function' + (', ' + params if params.strip() else '')
            body = body.replace('__func__', '#function')
        if name in ('unit_test', 'group_test_setup', 'group_test_teardown'):
            kind = {'unit_test':'TEST','group_test_setup':'GROUP_SETUP','group_test_teardown':'GROUP_TEARDOWN'}[name]
            return '#define ' + name + '(f) {#f, f, UNIT_TEST_FUNCTION_TYPE_' + kind + '}'
        if name.startswith('assert_') and '_set' in name and name.split('_')[1] in ('int','uint','float'):
            # Public arrays have the documented facade element type. Native C99
            # bridges use checked heap conversion and native set diagnostics.
            epsilon = ', epsilon' if name.startswith('assert_float') else ''
            return '#define ' + name + '(value, values, count' + epsilon + ') do { if ((count) > 0) ' + ('_' + name if name.startswith('assert_float') else 'cpkt_cmocka_' + name) + '(value, values, count' + epsilon + ', __FILE__, __LINE__); } while (0)'
        return '#define ' + name + '(' + params + ')' + body
    text = macro_pattern.sub(rewrite, text)
    text = re.sub(r',\s*}', '\n}', text)
    text = re.sub(r'(#define assert_uint_in_range[^\n]*)', lambda m:m[0].replace('cast_to_intmax_type','cast_to_uintmax_type'), text)
    # Union/callback declarations use facade types; width conversions happen
    # only inside the private bridge, never by pretending their ABIs match.
    for name in ('_expect_check', '_expect_check_data'):
        text = re.sub(r'\b' + name + r'\b', 'cpkt_cmocka' + name, text)
    declarations += ['void cpkt_cmocka_expect_check_data(const char *, const char *, const char *, int, CpktCheckParameterValueData, CpktCMockaValueData, CpktCheckParameterEventData *, int);',
                     'void cpkt_cmocka_expect_check(const char *, const char *, const char *, int, CpktCheckParameterValue, unsigned long, CpktCheckParameterEvent *, int);']
    text = text.replace('cpkt_cmocka_expect_check(', 'cpkt_cmocka_expect_check(')
    text = re.sub(r'\b_has_mock\b', 'cpkt_cmocka_has_mock', text)
    text = re.sub(r'\b_cmocka_run_group_tests\b', 'cpkt_cmocka_run_group_tests', text)
    text = text.replace('mock_type(type) ((type) mock())', 'mock_type(function, type) ((type) mock(function))')
    text = text.replace('mock_parameter_type(name, type) ((type) mock_parameter(#name))', 'mock_parameter_type(function, name, type) ((type) mock_parameter(function, name))')
    text = re.sub(r'void cmocka_set_message_output\([^;]+;', 'void cpkt_cmocka_set_message_output(unsigned long output);\n#define cmocka_set_message_output cpkt_cmocka_set_message_output', text)
    def document(match):
        name=match.group(2)
        operation=name.removeprefix('cpkt_cmocka_').removesuffix('_words').replace('_',' ')
        detail='Full 64-bit values use high/low 32-bit words. ' if name.endswith('_words') else ''
        descriptions={
            'words':'Construct high/low words without narrowing either 32-bit half.',
            'location':'Set the explicit file and line used by fail_msg diagnostics.',
            'fail_msg':'Fail through native cmocka formatting and the explicit source location.',
            'mock':'Retrieve the native return queue value for an explicitly named function.',
            'mock_parameter':'Retrieve a native parameter queue value for an explicitly named function.',
            'unit_test_dummy':'Provide the no-op C89 setup/teardown callback for native test descriptors.'}
        descriptions['run group tests']='Run native tests with converted descriptors; transferred check-event storage remains alive through callbacks and is freed after consumption or runner cleanup.'
        descriptions['expect check data']='Transfer a supplied heap event to the facade; callback data may point into that allocation. Native queue behavior is preserved; storage is freed after consumption or runner cleanup.'
        descriptions['expect check']='Transfer a supplied heap event for a legacy scalar callback; retain its storage until consumption or runner cleanup.'
        description=descriptions.get(operation,'Bridge native cmocka '+operation+' with its assertion/queue diagnostics and ownership.')
        return '/** '+detail+description+' */\n'+match.group(1)
    declarations=[document(re.match(r'(.*?\b(cpkt_cmocka_\w+)\s*\(.*)',d)) if re.match(r'(.*?\b(cpkt_cmocka_\w+)\s*\(.*)',d) else d for d in declarations]
    # Document complete declarations, retaining their parameter lists verbatim.
    def declaration_doc(match):
        return document(match)
    text=re.sub(r'(?m)^([^#\n]*\b(cpkt_cmocka_\w+)\s*\([^;]*;)',declaration_doc,text)
    text = '#ifndef CPKT_CMOCKA_C89_H\n#define CPKT_CMOCKA_C89_H\n#ifdef __cplusplus\nextern "C" {\n#endif\n' + text + '\n' + '\n'.join(declarations) + '\n#ifdef __cplusplus\n}\n#endif\n#endif\n'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_generated_text(args.output, notice+'\n'+text)
    write_generated_text((args.output.parent / 'cmocka_types.h'), '#ifndef CPKT_CMOCKA_TYPES_H\n#define CPKT_CMOCKA_TYPES_H\n#include <stddef.h>\n#include <stdarg.h>\n' + types + '\n#endif\n')
    write_generated_text((args.output.parent / 'cmocka_bridge.inc'), '\n'.join(bridge) + '\n')
    write_generated_text((args.output.parent / 'cmocka_bridge_exports.txt'), '\n'.join(sorted(exports)) + '\n')


if __name__ == '__main__':
    main()
