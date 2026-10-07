#!/usr/bin/env python3
"""Synthetic Star Trek: Conquest "WII!" texture bundle (see
project/src/wszst_cmd/extract/wiitex.inc): one 8x8 RGB565 texture.
usage: mk_wiitex.py OUTDIR -> OUTDIR/TEST.WII"""
import os, struct, sys

w = h = 8
pix = b''.join(struct.pack('>H', (x * 4 << 11) | (y * 8 << 5) | 0x10) for y in range(h) for x in range(w))
out = bytearray(0x100)
out[0:4] = b'WII!'
struct.pack_into('>5I', out, 4, 2, 1, 0, 0, 0)
struct.pack_into('>I', out, 32, 0x40)
out[0x40:0x44] = b'TEST'
struct.pack_into('>HHIIII', out, 0x44, w, h, 1, 0, len(pix), 0x100)
out += pix
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.WII'), 'wb').write(bytes(out))
