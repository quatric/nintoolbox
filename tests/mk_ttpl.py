#!/usr/bin/env python3
"""Synthetic Petz engine TTPL texture (see project/src/wszst_cmd/extract/ttpl.inc):
an 8x4 I8 TPL behind the {"TTPL", size} wrapper. usage: mk_ttpl.py OUTDIR -> OUTDIR/tx_testtxl"""
import os, struct, sys

w, h = 8, 4
pixels = bytes((x * 32 + y * 8) & 0xff for y in range(h) for x in range(w))
data_off = 0x40
tpl = struct.pack('>III', 0x0020af30, 1, 0x0c) + struct.pack('>II', 0x14, 0)
tpl += struct.pack('>HHIIIIIIfBBBB', h, w, 1, data_off, 0, 0, 1, 1, 0.0, 0, 0, 0, 0)
tpl = tpl.ljust(data_off, b'\0') + pixels
out = b'TTPL' + struct.pack('>I', len(tpl)) + tpl
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'tx_testtxl'), 'wb').write(out)
