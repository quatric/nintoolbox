#!/usr/bin/env python3
"""Synthetic GAN2 (.GA2) with three tracks ROOT > HIP > KNEE. usage: mk_gan2.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
b = bytearray(0x30 + 0x30 * 3)
b[0:4] = b'GAN2'; struct.pack_into('>I', b, 4, 1); struct.pack_into('<II', b, 0x0c, len(b), 0); struct.pack_into('<I', b, 0x14, 3)
for i, (nm, par, ch, dat) in enumerate([(b'ROOT', 0, 0x60, 0), (b'HIP', 0x30, 0x90, 0x100), (b'KNEE', 0x60, 0, 0x120)]):
    o = 0x30 + 0x30 * i
    b[o:o + len(nm)] = nm
    struct.pack_into('<5I', b, o + 16, par, ch, 0, 0, dat)
open(os.path.join(d, 't.GA2'), 'wb').write(bytes(b))
