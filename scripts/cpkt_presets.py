"""Resolve the authoritative preset inheritance and owned configuration name."""
import json
from pathlib import Path


def preset_info(root, preset):
    definitions = {p['name']: p for p in json.loads((Path(root) / 'CMakePresets.json').read_text())['configurePresets']}
    if preset not in definitions or definitions[preset].get('hidden'):
        raise RuntimeError('unknown preset: ' + preset)
    def resolve(name):
        item = definitions[name]
        result = {'cacheVariables': {}, 'environment': {}}
        parents = item.get('inherits', [])
        for parent in reversed([parents] if isinstance(parents, str) else parents):
            inherited = resolve(parent)
            result.update({k: v for k, v in inherited.items() if k not in ('cacheVariables','environment')})
            result['cacheVariables'].update(inherited['cacheVariables'])
            result['environment'].update(inherited.get('environment',{}))
        result.update({k: v for k, v in item.items() if k not in ('cacheVariables','environment')})
        result['cacheVariables'].update(item.get('cacheVariables', {}))
        result['environment'].update(item.get('environment', {}))
        return result
    item = resolve(preset)
    variables = item['cacheVariables']
    target = variables['CPKT_TARGET_ARCH'] + ('-apple-darwin' if variables['CPKT_TARGET_OS'] == 'darwin'
             else '-linux-' + variables['CPKT_TARGET_LIBC'])
    configuration = {'valgrind': 'Valgrind', 'fuzz': 'Fuzz', 'opcua-fuzz': 'Fuzz'}.get(preset, variables['CMAKE_BUILD_TYPE'])
    return item, target, configuration
