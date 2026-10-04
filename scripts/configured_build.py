"""Resolve the configured SDK build used by source-tree contract tools.

CTest supplies its binary directory. Standalone commands use the normal
per-target preset; a missing cache is an error, never a host-tool fallback.
"""

import os
from pathlib import Path
from cpkt_inventory import GROUPS, REPOSITORY_GROUP
from cpkt_presets import preset_info


def binary_dir(root, target):
    selected = os.environ.get("CPKT_CONFIGURED_BINARY_DIR")
    group = os.environ.get("CPKT_CONFIGURED_GROUP", os.environ.get("GROUP", "all"))
    if group not in GROUPS:
        raise RuntimeError('unknown GROUP: '+group)
    if group == 'all':group=REPOSITORY_GROUP
    if selected:
        directory = Path(selected).resolve()
    else:
        preset = os.environ.get('PRESET', os.environ.get('CPKT_PRESET',
                 'debug' if target == 'x86_64-linux-gnu' else target+'-release'))
        _, resolved, configuration = preset_info(root,preset)
        if resolved != target:
            raise RuntimeError('PRESET='+preset+' resolves to '+resolved+', expected '+target)
        directory = root / 'build' / target / group / configuration
    if not (directory / "CMakeCache.txt").is_file():
        raise RuntimeError("configured CMake cache missing: " + str(directory))
    values={line.split(':',1)[0]:line.split('=',1)[1]
            for line in (directory/'CMakeCache.txt').read_text().splitlines()
            if ':' in line and '=' in line and not line.startswith(('#','//'))}
    if values.get('CPKT_TARGET_ID',target) != target:
        raise RuntimeError('configured target does not match '+target+': '+str(directory))
    if group != 'all' and values.get('CPKT_GROUP',group) != group:
        raise RuntimeError('configured group does not match '+group+': '+str(directory))
    return directory


def cache_value(root, target, name):
    cache = (binary_dir(root, target) / "CMakeCache.txt").read_text()
    for line in cache.splitlines():
        if line.startswith(name + ":") and "=" in line:
            return line.split("=", 1)[1]
    raise RuntimeError("configured {} missing for {}".format(name, target))


def scratch_dir(root, target, name):
    return binary_dir(root, target) / name
