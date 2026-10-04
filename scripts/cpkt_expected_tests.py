#!/usr/bin/env python3
"""Generate required-case assertions from explicit inventory registrations."""
import argparse
import json
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--root',type=Path,required=True)
parser.add_argument('--group',required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--composition',action='store_true')
parser.add_argument('--profile')
args=parser.parse_args()
data=json.loads((args.root/'cmake/components.json').read_text())
lines=[]
for name,item in data['tests'].items():
    owners=('all','tooling') if args.composition else (args.group,'tooling')
    if item['group'] not in owners:continue
    if args.profile and name not in data['groups'][args.group]['profiles'][args.profile]['tests']:
        continue
    for registration in item.get('registrations',[]):
        conditions=registration['conditions']
        condition=' AND '.join('('+value+')' for value in conditions) or 'TRUE'
        expected=registration['name']
        lines+=['if('+condition+')', '  if(NOT TEST "'+expected+'")',
                '    message(FATAL_ERROR "Missing required inventory case: '+expected+'")',
                '  endif()', 'endif()']
lines += ['get_property(_cpkt_required DIRECTORY PROPERTY TESTS)',
          'list(JOIN _cpkt_required "\\n" _cpkt_required_lines)',
          'file(WRITE "${CMAKE_BINARY_DIR}/cpkt-required-coverage.txt" "${_cpkt_required_lines}\\n")']
args.output.write_text('\n'.join(lines)+'\n')
