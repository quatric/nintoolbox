#!/usr/bin/env python3
"""Synthetic SEGA HSPR sprite sheet (Sakura Wars .hspr; see project/src/wszst_cmd/extract/sega_hspr.inc): HSPR wrapping a KSPR chunk
with two sprite records.
usage: mk_sega_hspr.py OUT.hspr"""
import struct, sys
S = bytearray(0x200)
S[0:4] = b'HSPR'; struct.pack_into('<III', S, 4, len(S) - 0x10, 0x10, 0)
S[0x10:0x14] = b'KSPR'; struct.pack_into('<III', S, 0x14, len(S) - 0x20, 0x10, 0)
struct.pack_into('<IIII', S, 0x20, 1, 0, 2, 0x30)
struct.pack_into('<IIII', S, 0x30, 2, 0x80, 2, 0x100)       # sprite count, record table, count, second table
for i, (kind, w, h) in enumerate(((10, 10.0, 40.0), (2, 30.0, 20.5))):
    struct.pack_into('<IIII2f2I', S, 0x80 + 0x10 + 0x20 * i, i, 0, kind, 0, w, h, 0x150 + 0x20 * i, 0x170 + 0x20 * i)
open(sys.argv[1], 'wb').write(S)
