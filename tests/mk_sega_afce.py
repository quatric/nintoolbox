#!/usr/bin/env python3
"""Synthetic SEGA AFCE face set (Sakura Wars MRS_KAO_DUMMY.GRD; see project/src/wszst_cmd/extract/sega_grd.inc): header, a 16 byte table and one
Melt packed HTEX chunk (literals only) holding a 0x40 byte "HTSF" container.
usage: mk_sega_afce.py OUT.GRD"""
import struct, sys
htsf = b'HTSF' + struct.pack('<III', 0x30, 0x10, 0) + bytes(range(0x30))
stream = bytearray(); nb = 0; fpos = 0
def flag(bit):
    global nb, fpos
    if nb == 0:
        fpos = len(stream); stream.append(0); nb = 8
    if not bit:
        stream[fpos] |= 1 << (8 - nb)
    nb -= 1
for b in htsf:
    flag(1); stream.append(~b & 0xff)
flag(0); flag(1); stream.extend([0xff, 0xff])
htex = bytearray(0x20)
htex[0:4] = b'HTEX'; struct.pack_into('<III', htex, 4, len(stream) + 0x10, 0x10, 0x400)
struct.pack_into('<II', htex, 0x10, len(htsf), 0x8000); htex[0x18:0x20] = b'\xff' * 8
table = struct.pack('<4I', 2, 0x10002, 1, 0x10001)
body = struct.pack('<4I', 4, 0x20, 0, 0) + table
S = b'AFCE' + struct.pack('<III', len(body) + len(htex) + len(stream) - 0x20 + 0x10, 0x10, 0) + body + bytes(htex) + bytes(stream)
open(sys.argv[1], 'wb').write(S)
