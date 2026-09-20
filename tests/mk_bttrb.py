#!/usr/bin/env python3
"""Synthetic Blue Tongue "TRB\\0" package (see project/src/lib-bttrb.h): one 8x8
RGB5A3 "ttex" texture in the __s00000 pool and an XUR section.
usage: mk_bttrb.py OUTDIR -> OUTDIR/T.trb"""
import os, struct, sys
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
f = bytearray(0x3000)
struct.pack_into('>4s5I', f, 0, b'TRB\0', 0x7d1, 2, 4, 0x30 * 4, 1)
strs = b'.text\0.data\0__s00000\0XUR\0ttex\0'
f[0x800:0x800 + len(strs)] = strs
secs = [(0, 0x800, len(strs)), (6, 0x1000, 0x100), (0xc, 0x2000, 128), (0x15, 0x2800, 8)]
for i, (name, off, size) in enumerate(secs):
    struct.pack_into('>4I2I', f, 0x80 + 0x30 * i, 0, name, 0, 0x40000, size, size)
    struct.pack_into('>I', f, 0x80 + 0x30 * i + 0x18, off)
struct.pack_into('>4I', f, 0x140, 0, 0, 1 << 16, 0x19)              # ttex symbol at .data + 0
obj = 0x1000
struct.pack_into('>3I', f, obj, 0, 0xa0, 0x12345678)
struct.pack_into('>2I', f, obj + 0xc, 0x0df00bb0, 0x0df00bb0)
F = obj + 0x14
struct.pack_into('>I', f, F, 5)                                       # RGB5A3
struct.pack_into('>I', f, F + 0x1c, 128)
struct.pack_into('>4H', f, F + 0x40, 8, 8, 8, 8)
f[F + 0x50:F + 0x50 + 9] = b'test.tga\0'
for i in range(64):                                                   # opaque red
    struct.pack_into('>H', f, 0x2000 + 2 * i, 0xfc00)
f[0x2800:0x2808] = b'XUIBTEST'
open(os.path.join(out, 'T.trb'), 'wb').write(f)
