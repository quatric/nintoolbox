#!/usr/bin/env python3
"""Synthetic Star Trek: Conquest mixed "WII!" bundle (see
project/src/wszst_cmd/extract/wiitex.inc): one 8x8 RGB565 texture, nameless record.
usage: mk_wiitex_b.py OUTDIR -> OUTDIR/LVL.wii"""
import os, struct, sys

w = h = 8
pix = b''.join(struct.pack('>H', (x * 4 << 11) | (y * 8 << 5) | 0x10) for y in range(h) for x in range(w))
out = bytearray(0x100)
out[0:4] = b'WII!'
struct.pack_into('>6I', out, 4, 2, 1, 1, 1, 1, 0)
struct.pack_into('>I', out, 32, 0x40)
struct.pack_into('>IHHHHIII', out, 0x40, 0, w, h, 6, 1, 0, len(pix), 0x100)
out += pix
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'LVL.wii'), 'wb').write(bytes(out))
