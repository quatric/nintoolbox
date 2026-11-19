#!/usr/bin/env python3
"""Synthetic SEGA HSPR sprite sheet (Sakura Wars .hspr; see project/src/wszst_cmd/extract/sega_hspr.inc): HSPR wrapping a KSPR chunk
with two sprite records.
usage: mk_sega_hspr.py OUT.hspr [packed]   (packed: Melt compressed ".bfX" form, header flags 0x400, literals only)"""
import struct, sys
S = bytearray(0x200)
S[0:4] = b'HSPR'; struct.pack_into('<III', S, 4, len(S) - 0x10, 0x10, 0)
S[0x10:0x14] = b'KSPR'; struct.pack_into('<III', S, 0x14, len(S) - 0x20, 0x10, 0)
struct.pack_into('<IIII', S, 0x20, 1, 0, 2, 0x30)
struct.pack_into('<IIII', S, 0x30, 2, 0x80, 2, 0x100)       # sprite count, record table, count, second table
for i, (kind, w, h) in enumerate(((10, 10.0, 40.0), (2, 30.0, 20.5))):
    struct.pack_into('<IIII2f2I', S, 0x80 + 0x10 + 0x20 * i, i, 0, kind, 0, w, h, 0x150 + 0x20 * i, 0x170 + 0x20 * i)
# animation track table of sprite 0 (B = 0x170): one float track with two keys; offsets in the entry are relative to the KSPR chunk
struct.pack_into('<IIII', S, 0x180, 1, 2, 0x180, 0x190)
struct.pack_into('<HH', S, 0x190, 10, 20)
struct.pack_into('<Iff', S, 0x1a0, 2, -10.0, 7.5)
struct.pack_into('<HHfff', S, 0x1b0, 1, 10, 69.0, 0, 0)
struct.pack_into('<HHfff', S, 0x1c0, 1, 20, 70.0, 0, 0)
if len(sys.argv) > 2:
    body = bytes(S[0x10:])          # the KSPR chunk is what the packed stream holds
    stream = bytearray(); nb = 0; fpos = 0
    def flag(bit):
        global nb, fpos
        if nb == 0:
            fpos = len(stream); stream.append(0); nb = 8
        if not bit:                 # stored flag byte is inverted
            stream[fpos] |= 1 << (8 - nb)
        nb -= 1
    for b in body:
        flag(1); stream.append(~b & 0xff)
    flag(0); flag(1); stream.extend([0xff, 0xff])
    hdr = bytearray(0x20)
    hdr[0:4] = b'HSPR'; struct.pack_into('<IIII', hdr, 4, len(stream) + 0x10, 0x10, 0x400, 0)
    struct.pack_into('<II', hdr, 0x10, len(body), 0x8000); hdr[0x18:0x20] = b'\xff' * 8
    S = hdr + stream
open(sys.argv[1], 'wb').write(S)
