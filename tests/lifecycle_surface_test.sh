#!/usr/bin/env bash
set -euo pipefail
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 - "$repo_root" <<'PY'
import json,subprocess,sys
from pathlib import Path
root=Path(sys.argv[1]);data=json.loads((root/'cmake/components.json').read_text())
owner=data['repository_group']
assert set(data['groups'])=={owner}
help_output=subprocess.run(['make','--no-print-directory','help'],cwd=root,check=True,text=True,capture_output=True).stdout
for target in ('finalize-slice','format','format-check','test-all','release','release-final-matrix','test-darwin-native','test-darwin-sdk'):
    assert target in help_output,target
for other in {'core','db','misc'}-{owner}:
    before=set(root.glob('.cache/*'))
    result=subprocess.run(['bash','scripts/lifecycle.sh','debug','--group',other],cwd=root,text=True,capture_output=True)
    assert result.returncode!=0,(other,result.stdout,result.stderr)
    assert before==set(root.glob('.cache/*'))
for target in ('release','test-all','finalize-slice'):
    result=subprocess.run(['make','--no-print-directory','-n',target],cwd=root,check=True,text=True,capture_output=True)
    assert 'lifecycle.sh "'+target+'"' in result.stdout
ordinary=['preflight','debug']+(['e2e-postgres'] if owner=='db' else [])+['clangd-surface','valgrind']+(['fuzz-smoke'] if owner!='db' else [])
recipes={'release':['lifecycle-version-contract','clean','format','format-check']+ordinary+['release-final-matrix'],'release-pipeline':['format','format-check']+ordinary+['release-matrix'],'test-all':ordinary}
for action,phases in recipes.items():
    subprocess.run([sys.executable,str(root/'tests/lifecycle_recipe_contract.py'),action,'\n'.join(phases)],check=True)
if owner=='core':assert (root/'skills/pkt-systems-cmake-lifecycle/SKILL.md').is_file()
else:
    assert not (root/'skills').exists()
    assert 'deps-core' in help_output
    pin=json.loads((root/'dependencies/cpkt.json').read_text())
    assert pin['repository']=='c89systems/cpkt'
print('independent lifecycle commands and wrong-owner rejection passed')
PY
