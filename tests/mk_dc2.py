#!/usr/bin/env python3
"""Synthetic DC2 assets (see project/src/lib-dc2.h): an 8x8 solid-red CMPR .dct
inside a .dcx archive. usage: mk_dc2.py OUTDIR -> OUTDIR/maps.dcx"""
import os, struct, sys

hdr = bytearray(0x3e)
hdr[0:4] = b'DC2\0'
for off, v in ((0x1b, 8), (0x1f, 8), (0x23, 8), (0x27, 8), (0x2b, 1), (0x3a, 32)):
    hdr[off:off + 4] = struct.pack('>I', v)
tile = struct.pack('>HHI', 0xF800, 0x0000, 0) * 4   # four solid-red sub-blocks
dct = bytes(hdr) + tile + b'\0' * 28

members = [('Textures\\red.DCT', dct), ('Misc\\note.xml', b'<a/>')]
dirsize = 4 + sum(4 + len(n) + 8 for n, _ in members)
out = struct.pack('>I', len(members))
off = dirsize
for n, d in members:
    out += struct.pack('>I', len(n)) + n.encode() + struct.pack('>II', off, len(d))
    off += len(d)
out += b''.join(d for _, d in members)
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'maps.dcx'), 'wb').write(out)
