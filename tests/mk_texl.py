#!/usr/bin/env python3
"""Synthetic Petz engine TEXL texture (see project/src/wszst_cmd/extract/texl.inc):
an 8x8 CMPR image. usage: mk_texl.py OUTDIR -> OUTDIR/tx_testtexl"""
import os, struct, sys

w = h = 8
block = struct.pack('>HHI', 0xf800, 0x001f, 0x1b1b1b1b)
hdr = b'TEXL' + struct.pack('>IIIHHI', 3, 0, 0, w, h, 0) + b'\0'
out = hdr + block * 4 + b'\0' * 8
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'tx_testtexl'), 'wb').write(out)
