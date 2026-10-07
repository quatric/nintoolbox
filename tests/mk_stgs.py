#!/usr/bin/env python3
"""Synthetic MySims Agents "STGS" string table (see project/src/wszst_cmd/extract/stgs.inc).
usage: mk_stgs.py OUTDIR -> OUTDIR/msg.str"""
import os, struct, sys

strs = [(0x06a45fee, b'namn'), (0x0fdf53d1, b'pratar')]
offs, body = [], b''
for _, s in strs:
    offs.append(len(body))
    body += s + b'\0'
out = b'STGS' + struct.pack('>3I', 5, len(strs), 0)
for (h, _), o in zip(strs, offs):
    out += struct.pack('>2I', h, o)
out += body
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'msg.str'), 'wb').write(out)
