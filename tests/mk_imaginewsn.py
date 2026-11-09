#!/usr/bin/env python3
"""Synthetic Imagine .wsn scene mesh: 1 object, 1 mesh (3 vertices = 1 triangle), 1 RGB565 4x4 texture. usage: mk_imaginewsn.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
b = bytearray()
OBJ = 0x30
b += struct.pack('>4I', 1, 1, 0x50, 0xcdcdcdcd)
b += struct.pack('>I', OBJ)                 # object offsets
b += struct.pack('>I', 0)                   # texture offset (patched)
b += struct.pack('>I', 4)                   # edge 4
b += struct.pack('>I', 0)                   # format 0 = RGB565
b = b.ljust(OBJ, b'\0')
o = bytearray(0x40)
struct.pack_into('>I', o, 0, 1)             # one mesh
MESH = OBJ + 0x40
struct.pack_into('>I', o, 0x38, MESH)
b += o
m = bytearray(0x30)
struct.pack_into('>I', m, 0, 3)
struct.pack_into('>I', m, 0x20, MESH + 0x30)
b += m
for x, y, z, rgb in [(0, 0, 0, (255, 0, 0)), (10, 0, 0, (0, 255, 0)), (0, 10, 5, (0, 0, 255))]:
    b += struct.pack('>5fI', x, y, z, 1.1, 0.93, 0) + bytes(rgb) + b'\xff'
struct.pack_into('>I', b, 0x14, len(b))     # texture offset
b += struct.pack('>16H', *([0xf800] * 16))   # 4x4 RGB565 red tile
open(os.path.join(d, 't.wsn'), 'wb').write(bytes(b))
# Beastly variant: vertex records pointed to from mesh +0x28 instead of +0x20
c = bytearray(b)
m0 = MESH
struct.pack_into('>I', c, m0 + 0x20, 0)
struct.pack_into('>I', c, m0 + 0x28, MESH + 0x30)
open(os.path.join(d, 'b.wsn'), 'wb').write(bytes(c))
