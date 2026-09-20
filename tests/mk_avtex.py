#!/usr/bin/env python3
"""Synthetic Avalanche .thb/.tbb texture files (see project/src/lib-avtex.h):
tex.thb/.tbb (one 8x8 RGB5A3) and atlas.thb/.tbb (two 8x8 RGB5A3).
usage: mk_avtex.py OUTDIR"""
import os, struct, sys

pix = struct.pack('>H', 0xFFFF) * 64  # opaque white, 8x8 RGB5A3

def record(size):
    return struct.pack('>IIIHHHHIII', size, size + 0x20, size, 8, 8, 5, 1, 8 << 16 | 8, 1, size)

def make(n):
    table_end = 4 + 12 * n
    thb = struct.pack('>I', n)
    for i in range(n):
        thb += struct.pack('>III', table_end + 32 * i, len(pix) * i, len(pix))
    for i in range(n):
        thb += record(len(pix))
    return thb, pix * n

os.makedirs(sys.argv[1], exist_ok=True)
for name, n in (('tex', 1), ('atlas', 2)):
    thb, tbb = make(n)
    open(os.path.join(sys.argv[1], name + '.thb'), 'wb').write(thb)
    open(os.path.join(sys.argv[1], name + '.tbb'), 'wb').write(tbb)
