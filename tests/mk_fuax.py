#!/usr/bin/env python3
"""Synthetic Rebel Raiders "FUAX" string table (see project/src/wszst_cmd/extract/fuax.inc).
usage: mk_fuax.py OUTDIR -> OUTDIR/msg.bin"""
import os, struct, sys

strs = ['Ghost team', 'Café €']
base = 0x14 + 4 * len(strs)
offs, body = [], b''
for s in strs:
    offs.append(len(body))
    body += s.encode('utf-16-be') + b'\0\0'
out = b'FUAX' + struct.pack('>I', 0x01321b4a) + b'engl' + struct.pack('>II', 0, len(strs)) \
    + b''.join(struct.pack('>I', o) for o in offs) + body
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'msg.bin'), 'wb').write(out)
