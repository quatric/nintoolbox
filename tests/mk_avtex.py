#!/usr/bin/env python3
"""Synthetic Avalanche .thb/.tbb texture pair (see project/src/lib-avtex.h): 8x8
RGB5A3. usage: mk_avtex.py OUTDIR -> OUTDIR/tex.thb + tex.tbb"""
import os, struct, sys

pix = struct.pack('>H', 0xFFFF) * 64  # opaque white
thb = struct.pack('>13I', 1, 0x10, 0, len(pix), len(pix), len(pix) + 0x20, len(pix), 0, 0, 0, 0, 0, 0)[:0x1c]
thb += struct.pack('>HHHH', 8, 8, 5, 1) + struct.pack('>HHII', 8, 8, 1, len(pix))
assert len(thb) == 48, len(thb)
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'tex.thb'), 'wb').write(thb)
open(os.path.join(sys.argv[1], 'tex.tbb'), 'wb').write(pix)
