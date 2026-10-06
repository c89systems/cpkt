"""Read native CMake configuration; do not interpret preset inheritance."""
from pathlib import Path
import subprocess
from generated_output_paths import validate_output_paths


def preset_info(root, preset):
    from cpkt_receipts import cache
    root=Path(root)
    binary=Path(subprocess.check_output(['bash',str(root/'scripts/build.sh'),'path','--group','all','--preset',preset],text=True).strip())
    validate_output_paths(binary)
    target, group, configuration=binary.relative_to(root/'build').parts
    configured=cache(binary/'CMakeCache.txt') if (binary/'CMakeCache.txt').is_file() else {}
    # The documented layout provides only the operation profile. All compiler,
    # feature, generator and flag decisions come from CMake's actual cache.
    expected=target.split('-')
    variables=dict(configured)
    variables.setdefault('CPKT_TARGET_ARCH',expected[0])
    variables.setdefault('CPKT_TARGET_OS','darwin' if target.endswith('darwin') else 'linux')
    variables.setdefault('CPKT_TARGET_LIBC',expected[-1])
    variables.setdefault('CMAKE_BUILD_TYPE','Release' if configuration=='Release' else 'Debug')
    return {'cacheVariables':variables,'generator':configured.get('CMAKE_GENERATOR','Ninja'),'binaryDir':str(binary)},target,configuration
