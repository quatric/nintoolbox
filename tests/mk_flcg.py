#!/usr/bin/env python3
"""Synthetic Grasshopper FLCG collision (.GCL): one model (origin 10,0,0), two triangles, one material. usage: mk_flcg.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
MAT, MOD, DATA, TRI = 0x40, 0x60, 0xc0, 0x100
b = bytearray(0x200)
b[0:4] = b'FLCG'; struct.pack_into('<I', b, 4, 1)
struct.pack_into('>HHII', b, 12, 1, 1, MOD, MAT)
b[MAT:MAT + 8] = b'WALL\0\0\0\0'
b[MOD:MOD + 8] = b'MODEL1\0\0'
struct.pack_into('>3f', b, MOD + 0x20, 10.0, 0.0, 0.0)
struct.pack_into('>I', b, MOD + 0x38, DATA)
struct.pack_into('>IIII', b, DATA, DATA + 40, TRI, 0, 2)
for i, tri in enumerate([((0, 0, 0), (1, 0, 0), (0, 1, 0)), ((1, 0, 0), (1, 1, 0), (0, 1, 0))]):
    struct.pack_into('>III', b, TRI + 48 * i, TRI + 48 * (i + 1) if i == 0 else 0, 0, MAT)
    struct.pack_into('>9f', b, TRI + 48 * i + 12, *[c for v in tri for c in v])
open(os.path.join(d, 't.GCL'), 'wb').write(bytes(b))
