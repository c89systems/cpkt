#!/usr/bin/env python3
"""Validate installed SDK structure and encode portable ABI/content manifests.

CMake installs files and writes compiled component metadata. This specialist
validator reads native ELF/Mach-O identities and emits the shared canonical
manifest encoding; it neither stages files nor runs a build/release pipeline.
"""
import argparse
import fnmatch
import json
from pathlib import Path
import sys
from cpkt_inventory import load
from cpkt_lock import delegated
from cpkt_packages import abi_records, make_manifest, prepared_core, safe_owned
from cpkt_receipts import cache, group_outputs, read, readiness_path, verification_inputs

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--group', required=True)
    parser.add_argument('--target', required=True)
    parser.add_argument('--preset', required=True)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    delegated(root, args.group)
    prefix = safe_owned(args.prefix)
    directory = root/'build'/args.target/args.group/'Release'
    configured = cache(directory/'CMakeCache.txt')
    proof = read(readiness_path(root, args.target, args.group, 'Release'))
    if (proof['verification_id'] != verification_inputs(root, args.group, configured)
            or proof['outputs'] != group_outputs(directory)):
        raise RuntimeError('package input is not the current tested Release graph')
    if configured['CPKT_BUNDLE_VERSION'] != args.version:
        raise RuntimeError('installed version differs from the configured build')
    data = load(root)
    components = json.loads((directory/'cpkt-package-components.json').read_text())
    abi = abi_records(prefix, configured)
    for component in components:
        package = data['components'][component['name']]['package']
        patterns = package['owned_patterns']+['lib/lib'+facade.get('output_name', facade['target'])+'.*'
                                               for facade in package['facades']]
        component['abi'] = {name: identity for name, identity in abi.items()
                            if any(fnmatch.fnmatchcase(name, pattern) for pattern in patterns)}
        if not component['abi']:
            if package['facades']:
                raise RuntimeError('missing component ABI surface: '+component['name'])
            component['abi'] = {'shared_surface': 'header-only'}
    core = None if args.group == 'core' else prepared_core(args.version, args.target, args.preset)[1]
    docs = prefix/'share/doc/cpkt'/args.group
    (docs/'THIRD_PARTY_NOTICES.md').write_text('Bundled '+args.group+' components and complete license paths:\n\n'+''.join(
        '- '+item['name']+' '+item['version']+': third_party/'+item['name']+'/LICENSE\n' for item in components))
    (docs/'README.md').write_text('cpkt '+args.version+' '+args.group+' SDK\n\nValidate this installation before CMake/pkg-config discovery. '+(
        'Independently usable core.' if core is None else 'Requires the exact core package identity recorded in packages/'+args.group+'.json.')+'\n')
    # Notices participate in the portable payload identity as ordinary files.
    make_manifest(prefix, args.group, args.version, args.target, components, core,
                  configured.get('CPKT_MACOS_DEPLOYMENT_TARGET', configured.get('CMAKE_OSX_DEPLOYMENT_TARGET')))

if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        sys.exit('SDK manifest: '+str(error))
