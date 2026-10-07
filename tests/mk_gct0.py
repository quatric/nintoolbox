#!/usr/bin/env python3
"""Synthetic Grasshopper "GCT0" texture (see project/src/wszst_cmd/extract/gct0.inc):
an 8x8 CMPR image. usage: mk_gct0.py OUTDIR -> OUTDIR/test.bin"""
import os, struct, sys

block = struct.pack('>HHI', 0xf800, 0x001f, 0x1b1b1b1b)
out = (b'GCT0' + struct.pack('>IHH', 14, 8, 8)).ljust(0x40, b'\0') + block * 4
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'test.bin'), 'wb').write(out)
