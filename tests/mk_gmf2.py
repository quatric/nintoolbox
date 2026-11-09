#!/usr/bin/env python3
"""Synthetic Grasshopper GMF2 model (.gm2): one object with one triangle (3 vertices, divisor 2^4), one material. usage: mk_gmf2.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
OBJ, MAT, SURF, DATA, VB = 0x70, 0xf0, 0x110, 0x130, 0x180
b = bytearray(0x200)
b[0:8] = b'GMF2' + struct.pack('<I', 2)
struct.pack_into('<4hIIII', b, 0x18, 1, 0, 0, 1, OBJ, 0x70, 0, MAT)
o = OBJ
b[o:o + 8] = b'TRI\0\0\0\0\0'
struct.pack_into('<7I', b, o + 8, 1, VB, 0, 0, 0, 0, SURF)
struct.pack_into('<i', b, o + 0x2c, 4)
struct.pack_into('<3f', b, o + 0x30, 1.0, 2.0, 3.0)
struct.pack_into('<3f', b, o + 0x50, 1.0, 1.0, 1.0)
b[MAT:MAT + 8] = b'MAT1\0\0\0\0'
struct.pack_into('<II', b, SURF + 8, DATA, MAT)
struct.pack_into('>IHh', b, DATA, 3 * 11 + 4, 3, 0)
q = DATA + 32
struct.pack_into('>HH', b, q, 0x99, 3); q += 4
for i in range(3):
    struct.pack_into('>HbbbHhh', b, q, i, 0, 0, 64, 0, 512 * i, 256); q += 11
for i, (x, y, z) in enumerate([(0, 0, 0), (16, 0, 0), (0, 16, 0)]):
    struct.pack_into('>3h', b, VB + 6 * i, x, y, z)
open(os.path.join(d, 't.gm2'), 'wb').write(bytes(b))
