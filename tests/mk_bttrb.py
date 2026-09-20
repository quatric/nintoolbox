#!/usr/bin/env python3
"""Synthetic Blue Tongue "TRB\\0" package (see project/src/lib-bttrb.h): one 8x8
RGB5A3 "ttex" texture in the __s00000 pool, an XUR section and a one-triangle
"tcmd" model.
usage: mk_bttrb.py OUTDIR -> OUTDIR/T.trb"""
import os, struct, sys
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
f = bytearray(0x4000)
struct.pack_into('>4s5I', f, 0, b'TRB\0', 0x7d1, 2, 5, 0x30 * 5, 2)
strs = b'.text\0.data\0__s00000\0XUR\0ttex\0gpu_data\0tri\0'
f[0x800:0x800 + len(strs)] = strs
secs = [(0, 0x800, len(strs)), (6, 0x1000, 0x400), (0xc, 0x2000, 128), (0x15, 0x2800, 8), (0x1e, 0x3000, 0x60)]
for i, (name, off, size) in enumerate(secs):
    struct.pack_into('>4I2I', f, 0x80 + 0x30 * i, 0, name, 0, 0x40000, size, size)
    struct.pack_into('>I', f, 0x80 + 0x30 * i + 0x18, off)
st = 0x80 + 0x30 * 5
struct.pack_into('>4I', f, st, 0, 0, 1 << 16, 0x19)                   # ttex symbol at .data + 0
struct.pack_into('>4I', f, st + 16, 0x646d6374, 0x300, 1 << 16, 0x27)  # tcmd symbol "tri"
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
# model: one batch {display list, positions, normals, TEX0, TEX1} in gpu_data
D, G = 0x1000, 0x3000
struct.pack_into('>5I', f, D + 0x100, 4, 0x06500309, 0x0210060a, 0x06500c0d, 0x0650080e)
rec = D + 0x200
struct.pack_into('>2I', f, rec + 4, 0x100, 1)
struct.pack_into('>I', f, rec + 0x14, 0x248)                          # array pairs (after the two display list words)
struct.pack_into('>2I', f, rec + 0x40, 16, 0)
for k, (arr, attr, size, cnt) in enumerate([(0x10, 9, 6, 3), (0x30, 10, 3, 1), (0x40, 13, 4, 3), (0x50, 14, 4, 1)]):
    struct.pack_into('>2I', f, rec + 0x48 + 8 * k, arr, attr << 24 | size << 16 | cnt)
struct.pack_into('>I', f, D + 0x2f0, 0x200)                           # batch offset array
struct.pack_into('>2I', f, D + 0x300 + 0x10, 1, 0x2f0)                # batch count, array
f[G:G + 15] = bytes([0x90, 0, 3, 0, 0, 0, 0, 1, 0, 1, 0, 2, 0, 2, 0])
struct.pack_into('>9h', f, G + 0x10, 0, 0, 0, 8, 0, 0, 0, 8, 0)
f[G + 0x30:G + 0x33] = bytes([0, 0, 64])
struct.pack_into('>6h', f, G + 0x40, 0, 0, 4096, 0, 0, 4096)
open(os.path.join(out, 'T.trb'), 'wb').write(f)
