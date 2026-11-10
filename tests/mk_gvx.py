#!/usr/bin/env python3
"""Synthetic GVX0 event script (Power Punch .pvx): 2 nodes, one B and one C record, a D block with a cue-name string. usage: mk_gvx.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
A = struct.pack('<4I', 0, 0, 0x10000, 1)
B = struct.pack('<6I', 0x1ffff, 0x10, 1, 0, 0, 0x51)
C = struct.pack('<4I', 0x51, 0x70001, 0x36, 1)
D = struct.pack('<I', 0x40400000) + struct.pack('<I', 0xfffffffe) + b'vc_chr_02/test_cue\0\0\0\0\0\0'
a = 0x30; b = a + len(A); c = b + len(B); dd = c + len(C); size = dd + len(D)
hdr = b'GVX0' + struct.pack('<10I', 0x116, 0, 0x28, a, b, c, dd, size, size, 2) + struct.pack('<I', 1)
open(os.path.join(d, 't.pvx'), 'wb').write(hdr + A + B + C + D)
