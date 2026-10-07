#!/usr/bin/env python3
"""Synthetic Kirby "GFMC" message file (see project/src/wszst_cmd/extract/gfmc.inc).
usage: mk_gfmc.py OUTDIR -> OUTDIR/msg.bin"""
import os, struct, sys

msgs = [(0x708ad01, 'Elegant Chair'), (0x708ad04, 'Soft Bed\nVery soft')]
pool = b''
offs = []
for _, t in msgs:
    offs.append(len(pool))
    pool += struct.pack('>5H', 0x000a, 0x5047, 0x000a, 0x5458, 0) + struct.pack('>H', len(t)) \
        + t.encode('utf-16-be') + struct.pack('>7H', 0x000b, 0x5047, 0x000a, 0x5047, 0x000a, 0x5458, 0)
s1 = 0x1c
s2 = s1 + 0x20
s3 = s2 + 12 * len(msgs)
s4 = s3 + 4 * len(msgs)
size = s4 + len(pool)
out = bytearray(size)
out[0:4] = b'GFMC'
struct.pack_into('>6I', out, 4, 0x10000, size, s1, s2, s3, s4)
for i, ((h, _), o) in enumerate(zip(msgs, offs)):
    struct.pack_into('>3I', out, s2 + 12 * i, h, 1, 4 * i)
    struct.pack_into('>I', out, s3 + 4 * i, o)
out[s4:] = pool
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'msg.bin'), 'wb').write(bytes(out))
