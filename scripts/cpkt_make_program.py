#!/usr/bin/env python3
"""Run CMake's native build tool under the repository operation lock."""
import os
from pathlib import Path
import sys

from cpkt_operation import delegated
from cpkt_receipts import cache, component_receipt, readiness_path
from cpkt_inventory import components_for, load


def main():
    root = Path(__file__).resolve().parent.parent
    group = os.environ['CPKT_OPERATION_SCOPE']
    configured = None
    for directory in (Path.cwd(), *Path.cwd().parents):
        if not directory.is_relative_to(root):
            break
        candidate = directory / 'CMakeCache.txt'
        if candidate.is_file():
            values = cache(candidate)
            if values.get('CPKT_NATIVE_MAKE_PROGRAM'):
                configured = values
                break
    if configured is None and os.environ.get('CPKT_NATIVE_MAKE_PROGRAM'):
        configured = {'CPKT_NATIVE_MAKE_PROGRAM': os.environ['CPKT_NATIVE_MAKE_PROGRAM']}
    if configured is None:
        raise RuntimeError('configured native build tool is missing')
    # Generated clean rules have no later launcher. Authenticate the graph's
    # actual owner before allowing either cleanup or ordinary native builds.
    delegated(root, configured.get('CPKT_GROUP', group))
    native = Path(configured['CPKT_NATIVE_MAKE_PROGRAM']).resolve()
    if native == Path(__file__).resolve() or not native.is_file():
        raise RuntimeError('configured native build tool is invalid')
    arguments = sys.argv[1:]
    if 'clean' in arguments or '--clean' in arguments:
        target = configured.get('CPKT_TARGET_ID')
        owner = configured.get('CPKT_GROUP')
        if target and owner in ('core', 'db', 'misc'):
            verification = root / 'build/verification' / target / owner
            for receipt in verification.glob('*-development.json'):
                receipt.unlink()
            for receipt in verification.glob('*-built.json'):
                receipt.unlink()
            if configured.get('CPKT_DEPENDENCY_PRODUCER') == 'ON':
                for name in components_for(load(root), owner):
                    component_receipt(root, target, name).unlink(missing_ok=True)
    os.execv(str(native), [str(native), *arguments])


if __name__ == '__main__':
    try:
        main()
    except (KeyError, RuntimeError, OSError) as error:
        sys.exit('Repository native build requires operation delegation: ' + str(error))
