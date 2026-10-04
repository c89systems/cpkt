#!/usr/bin/env python3
"""Independent inventory and real consumer policy for audio/speech packaging."""
import json
from pathlib import Path
import sys
root=Path(sys.argv[1]);data=json.loads((root/'cmake/components.json').read_text())
components=data['components'];misc=data['groups']['misc']['package']
assert components['miniaudio']['group']==components['whisper']['group']=='misc'
for component,license in [('miniaudio','miniaudio'),('whisper','whisper.cpp')]:
 item=components[component]['package'];assert item['license'].endswith('/LICENSE')
 native=root/'.cache/deps-build/x86_64-linux-gnu'/components[component]['directory']/item['license']
 if native.exists():assert native.stat().st_size>100
files={i['source']:i['path'] for i in misc['files']}
for name in ['LICENSE','PROVENANCE.md']:
 source='docs/third_party/kblab-whisper-models/'+name
 assert 'docs/third_party/kblab-whisper-models' in misc['extra_notices']
 assert (root/source).is_file()
assert 'KBLab/kb-whisper-*' in (root/'docs/third_party/kblab-whisper-models/PROVENANCE.md').read_text()
assert 'docs/sus-model-catalog.tsv' in files
catalog=(root/'docs/sus-model-catalog.tsv').read_text().splitlines()
tiny=[l.split('\t') for l in catalog if 'ggml-tiny.bin\t' in l]
assert len(tiny)==1 and 'https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin' in tiny[0]
assert '1' in tiny[0]
assert not any(l.startswith('ggml-small.bin\t') and l.endswith('\t1') for l in catalog)
records=data['installed_consumers']
sus=[i for i in records.values() if i.get('pc')=='cpkt-sus']
assert any('@prefix/lib/libcpktaudio.a' in i.get('link_contains',[]) for i in sus)
assert any('strcmp(entry.name, "tiny")' in (root/i['source']).read_text() for i in sus)
consumer=(root/'scripts/cpkt_sdk_consumer.py').read_text()
assert "words.count('-lcpktaudio')!=1" in consumer
assert 'item.get(\'link_contains\',[])' in consumer
for name in ['audio-sus-c89','audio-live-vox-c89','audio-vox-intro-c89','sus-live-vox-c89','sus-vox-intro-c89']:
 assert 'examples/'+name+'/CMakeLists.txt' in misc['examples']
 assert 'examples/'+name+'/main.c' in misc['examples']
print('audio/speech split payload, default catalog, provenance, examples and retained static closure assertions passed')
