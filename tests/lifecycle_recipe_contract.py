#!/usr/bin/env python3
"""Observe direct lifecycle recipe calls without executing operational children."""
from contextlib import nullcontext
from pathlib import Path
import sys
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import cpkt_lifecycle as lifecycle
selected=sys.argv[1];expected=sys.argv[2].splitlines();actual=[]
def profile(frame,event,arg):
    if event=='call' and frame.f_code is lifecycle.perform.__code__:
        parent=frame.f_back
        if parent and parent.f_code.co_name=='child' and parent.f_back.f_locals.get('action')==selected:actual.append(frame.f_locals['action'])
with patch.object(lifecycle,'command'),patch.object(lifecycle,'backend'),patch.object(lifecycle,'child_delegation',return_value=nullcontext(({},()))),patch.object(lifecycle.subprocess,'run'),patch.object(lifecycle.subprocess,'check_output',return_value='0.0.0\n'):
    sys.setprofile(profile)
    try:lifecycle.perform(selected,'all','debug',False,'',False,'')
    finally:sys.setprofile(None)
if actual!=expected:sys.exit('recipe '+selected+' expected '+repr(expected)+' observed '+repr(actual))
print('serialized lifecycle recipe passed: '+selected)
