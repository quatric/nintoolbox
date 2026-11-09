#!/usr/bin/env python3
"""Synthetic Pangya .gsr texture (RGBA8 4x4, tex + texd chunks). usage: mk_gsr.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
def tag(t): return t[::-1]
pix = b'\xff\x10' * 16 + b'\x20\x30' * 16        # GX RGBA8 tile: AR x16, GB x16
b = struct.pack('>I', 0x20) + b'ffrg\0rsg' + struct.pack('>III', 0x60 + len(pix), 5, 2) + bytes(8)
b += tag(b'tex\0') + struct.pack('>III', 1, 0x18, 0x40)
b += tag(b'texd') + struct.pack('>III', len(pix), 1, 0x60)
b += struct.pack('>6I', 0x80000006, 4, 4, 1, len(pix), 0) + bytes(8)
b = b.ljust(0x60, b'\0') + pix
open(os.path.join(d, 't.gsr'), 'wb').write(b)
