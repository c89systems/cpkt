"""Read CMake command definitions without hashing unrelated registrations."""
import re
from cpkt_inventory import record


def commands(text):
    position = 0
    expression = re.compile(r'(?m)^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(')
    while True:
        match = expression.search(text, position)
        if not match:
            return
        index = match.end()
        depth, quoted, escaped = 1, False, False
        while index < len(text) and depth:
            char = text[index]
            if escaped:
                escaped = False
            elif char == '\\':
                escaped = True
            elif char == '"':
                quoted = not quoted
            elif not quoted:
                if char == '#':
                    newline = text.find('\n', index)
                    index = len(text) if newline < 0 else newline
                    continue
                if char == '(':
                    depth += 1
                elif char == ')':
                    depth -= 1
            index += 1
        yield match.group(1), text[match.end():index-1], text[match.start():index].strip()
        position = index


def owned_definitions(text, data, group):
    result = []
    for name, arguments, definition in commands(text):
        tokens = re.findall(r'[^\s]+', arguments)
        if not tokens:
            continue
        candidates = []
        if name == 'set' and tokens[0].startswith('CPKT_') and (
                tokens[0].endswith(('_ABI_VERSION', '_GENERATED_DIR')) or '_FACADE_' in tokens[0]):
            component = tokens[0][5:].split('_ABI_VERSION')[0].split('_GENERATED_DIR')[0].split('_FACADE_')[0]
            candidates = [('targets', 'cpkt_' + component.lower() + '_shared')]
        elif name == 'add_custom_command':
            component = re.search(r'CPKT_([A-Z0-9_]+)_(?:FACADE_|GENERATED_DIR)', definition)
            if component:
                candidates = [('targets', 'cpkt_' + component.group(1).lower() + '_shared')]
            elif '_cpkt_fixture_dir' in definition and 'tools/opcua/' in definition:
                candidates = [('targets', 'cpkt_opcua_shared')]
        elif name in ('add_test', 'cpkt_group_add_test') and tokens[0] == 'NAME':
            candidates = [('tests', tokens[1])]
        elif name in ('add_library','add_executable','add_custom_target',
                     'cpkt_group_add_library','cpkt_group_add_executable','cpkt_group_add_custom_target'):
            candidates = [('targets',tokens[0])]
        elif name == 'cpkt_group_target_command' and len(tokens)>1:
            candidates = [('targets',tokens[1])]
        elif name.startswith('cpkt_add_'):
            # Repository target helpers can create a target without an
            # add_library/add_executable call at their invocation site.
            candidates = [('targets',tokens[0])]
        elif name.startswith(('target_', 'cpkt_configure_', 'cpkt_add_repo_warning', 'cpkt_apply_auth_export', 'cpkt_group_use_local_runtime')):
            candidates = [('targets',tokens[0])]
        elif name in ('set_tests_properties','cpkt_group_set_tests_properties'):
            candidates = [('tests',token) for token in tokens[:tokens.index('PROPERTIES')]]
        elif name in ('set_target_properties','cpkt_group_set_target_properties'):
            candidates = [('targets',token) for token in tokens[:tokens.index('PROPERTIES')]]
        owners = []
        for kind, token in candidates:
            try:
                owners.append(data[kind][token]['group'] if token in data[kind] else record(data[kind],token)['group'])
            except RuntimeError:
                continue
        if group in owners:
            result.append(definition)
    return result
