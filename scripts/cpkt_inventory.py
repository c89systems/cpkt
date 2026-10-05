"""Authoritative package ownership and graph validation (standard library only)."""
import json
import re
from pathlib import Path

REPOSITORY_GROUP = json.loads((Path(__file__).resolve().parent.parent / 'cmake/components.json').read_text())['repository_group']
GROUPS = (REPOSITORY_GROUP, 'all')


def pattern(name):
    parts = re.split(r'(\$\{[^}]+\})', name)
    return '^' + ''.join('.+' if part.startswith('${') else re.escape(part) for part in parts) + '$'


def record(records, name):
    matches = [(key, value) for key, value in records.items() if re.fullmatch(pattern(key), name)]
    if matches:
        specificity = lambda item: len(re.sub(r'\$\{[^}]+\}', '', item[0]))
        strongest = max(map(specificity, matches))
        matches = [item for item in matches if specificity(item) == strongest]
    if len(matches) != 1:
        raise RuntimeError('inventory requires unique ownership for ' + name)
    return matches[0][1]


def load(root):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise RuntimeError('duplicate inventory key: ' + key)
            result[key] = value
        return result
    data = json.loads((Path(root) / 'cmake/components.json').read_text(), object_pairs_hook=unique)
    if data.get('schema_version') != 1 or data.get('repository_group') not in ('core','db','misc') or set(data['groups']) != {data.get('repository_group')}:
        raise RuntimeError('unsupported component inventory schema/groups')
    components = data['components']
    for group, item in data['groups'].items():
        if item['requires'] != ([] if group == 'core' else ['core']):
            raise RuntimeError('forbidden group prerequisites: ' + group)
        for dependency in item.get('core_components', []) + item.get('test_components', []):
            if dependency not in components or components[dependency]['group'] != 'core':
                raise RuntimeError('invalid core component prerequisite: ' + dependency)
    visiting, visited = set(), set()

    def visit(name):
        if name in visiting:
            raise RuntimeError('component dependency cycle: ' + name)
        if name in visited:
            return
        visiting.add(name)
        item = components[name]
        if item['group'] not in data['groups'] and item.get('external') != 'cpkt':
            raise RuntimeError('unknown component group: ' + name)
        for dependency in item['dependencies']:
            if dependency not in components:
                raise RuntimeError('unknown dependency: ' + dependency)
            owner = components[dependency]['group']
            if owner != item['group'] and owner != 'core':
                raise RuntimeError('forbidden group edge: ' + name + ' -> ' + dependency)
            visit(dependency)
        visiting.remove(name)
        visited.add(name)

    for name in components:
        visit(name)
    for kind in ('targets', 'tests'):
        for name, item in data[kind].items():
            if item['group'] not in (*data['groups'], 'all', 'tooling') and item.get('external') != 'cpkt':
                raise RuntimeError('unknown ' + kind + ' owner: ' + name)
            for required in item.get('requires', []):
                if required not in components and required not in data['groups'] and required != 'core':
                    raise RuntimeError('unknown prerequisite: ' + required)
                owner = components[required]['group'] if required in components else required
                if item['group'] in data['groups'] and owner not in (item['group'], 'core'):
                    raise RuntimeError('forbidden consumer edge: ' + name + ' -> ' + required)
    if 'installed_consumers' in data:
        if len(data.get('package_targets', [])) != 7 or len(set(data['package_targets'])) != 7:
            raise RuntimeError('package inventory must have seven unique targets')
        files = set()
        packages = {}
        for name, item in components.items():
            package = item.get('package')
            if not package or not package['owned_patterns'] or not package['license']:
                raise RuntimeError('missing package ownership: ' + name)
            for kind in ('cmake', 'pkgconfig'):
                for value in package[kind]:
                    identity = (kind, value)
                    if identity in packages:raise RuntimeError('duplicate package metadata owner: ' + value)
                    packages[identity] = item['group']
            for facade in package['facades']:
                files.update('include/cpkt/' + h for h in facade['headers'] if not h.startswith('@'))
                if 'export_catalog' in facade:files.add(facade['export_catalog'])
                elif not facade.get('export_policy'):raise RuntimeError('facade has no export boundary')
        for name, item in data['installed_consumers'].items():
            if item['group'] not in data['groups'] and item['group'] != 'all':raise RuntimeError('unknown installed consumer owner: ' + name)
            files.add(item['source']);files.update(item.get('extra_sources', []))
            for extra in item.get('pc_extra',[]):
                if packages.get(('pkgconfig',extra)) not in (item['group'],'core'):raise RuntimeError('installed consumer pkg-config sibling dependency: '+name+' -> '+extra)
            owner = packages.get(('cmake', item.get('package_directory',item['package'])))
            facade_owners = {f['cmake']:c['group'] for c in components.values() for f in c['package']['facades']}
            owner = owner or facade_owners.get(item['package'])
            if item['group'] != 'all' and owner not in (item['group'], 'core'):raise RuntimeError('installed consumer sibling dependency: ' + name)
        for item in data['groups'].values():
            files.update(item['package']['docs']);files.update(item['package']['examples'])
            files.update(f['source'] for f in item['package']['files'])
        for kind in ('targets', 'tests'):
            for item in data[kind].values():
                files.update(item.get('source_inputs', []));files.update(item.get('command_inputs', []))
    return data

def validate_inputs(root, data=None, group='all'):
    data = data or load(root)
    files = set()
    for component in components_for(data,group,prerequisites=True):
        files.update(data['components'][component].get('recipe_inputs',[]))
        files.update(data['components'][component].get('package',{}).get('extra_notices',[]))
    for kind in ('targets','tests'):
        for item in data[kind].values():
            if group == 'all' or item['group'] in (group,'tooling'):
                files.update(item.get('source_inputs',[]));files.update(item.get('command_inputs',[]));files.update(item.get('helper_inputs',[]))
    if 'installed_consumers' in data:
        for item in data['installed_consumers'].values():
            if group == 'all' or item['group'] == group:
                files.add(item['source']);files.update(item.get('extra_sources',[]))
        for owner,item in data['groups'].items():
            if group == 'all' or owner == group:
                hardening=item.get('hardening',{})
                if hardening.get('memcheck_suppression'):files.add(hardening['memcheck_suppression'])
                for seed in hardening.get('fuzz_seeds',[]):
                    files.add(seed)
                    path=Path(root)/seed
                    if path.is_file() and path.stat().st_size==0:
                        raise RuntimeError('fuzz seed must be nonempty: '+seed)
                files.update(item.get('verification_inputs',[]));files.update(item['package']['docs']);files.update(item['package']['examples']);files.update(f['source'] for f in item['package']['files'])
                for notice in item['package'].get('extra_notices',[]):
                    directory=Path(root)/notice
                    if not directory.is_dir() or directory.is_symlink():raise RuntimeError('notice source directory missing/unsafe: '+notice)
                    inputs=[p for p in directory.rglob('*') if p.is_file()]
                    if not inputs:raise RuntimeError('notice source directory empty: '+notice)
                    files.update(p.relative_to(root).as_posix() for p in inputs)
    pending=list(files)
    checked=set()
    suffix=r'(?:py|sh|cmake|hpp|h|cpp|cxx|cc|c|json|patch|series|txt)(?![A-Za-z0-9_.])'
    while pending:
        name=pending.pop()
        if name in checked or '${' in name:continue
        checked.add(name)
        path=Path(root)/name
        if not path.is_file():raise RuntimeError('inventory input missing: '+name)
        if path.suffix in ('.sh','.cmake','.py'):
            text=path.read_text()
            # Explicit source-root references are requirements, even when absent.
            # Other literal paths can denote fixture outputs or upstream trees.
            for child in ([] if path.suffix=='.py' else re.findall(r'(?:\$repo_root/|\$\{CMAKE_SOURCE_DIR\}/|\$\{PROJECT_SOURCE_DIR\}/)((?:scripts|tools|tests|cmake|skills)/[A-Za-z0-9_./-]+\.'+suffix+r')',text)):
                pending.append(child);files.add(child)
            for child in re.findall(r'(?:scripts|tools|tests|cmake)/[A-Za-z0-9_./-]+\.'+suffix,text):
                if (Path(root)/child).is_file():pending.append(child);files.add(child)
    return sorted(name for name in files if "${" not in name)


def components_for(data, group, prerequisites=False, tests=True):
    owned = {name for name, item in data['components'].items()
             if not item.get('external') and (group == 'all' or item['group'] == group)}
    if prerequisites:
        def add(name):
            for dependency in data['components'][name]['dependencies']:
                if dependency not in owned:
                    owned.add(dependency)
                    add(dependency)
        for name in list(owned):
            add(name)
        if group in ('db', 'misc'):
            for name in data['groups'][group].get('core_components', []) + (data['groups'][group].get('test_components', []) if tests else []):
                owned.add(name)
                add(name)
    # Stable topological order.
    result = []
    def append(name):
        if name in result:
            return
        for dependency in data['components'][name]['dependencies']:
            if dependency in owned:
                append(dependency)
        result.append(name)
    for name in sorted(owned):
        append(name)
    return result
