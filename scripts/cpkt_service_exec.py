#!/usr/bin/env python3
"""Do not let persistent service processes inherit repository lock descriptors."""
import os
import sys
for name in ('CPKT_OPERATION_FD','CPKT_OPERATION_CAP_FD'):
    value=os.environ.pop(name,None)
    if value:
        try:os.close(int(value))
        except OSError:pass
for name in ('CPKT_OPERATION_ROOT','CPKT_OPERATION_RUN','CPKT_OPERATION_SCOPE'):
    os.environ.pop(name,None)
os.execvp(sys.argv[1],sys.argv[1:])
