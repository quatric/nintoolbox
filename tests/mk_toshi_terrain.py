#!/usr/bin/env python3
"""Synthetic Toshi terrain model .trb (Nickelodeon Barnyard Wii, Data/Terrain; see project/src/lib-toshi.c):
world shader (LOD shader 2), two meshes listed in a table after the Collision symbol: mesh 0 stores its three
vertices inline (fmt 06 01 01 01: s16 x3 position, RGB565 colour, s16 x2 texcoord), mesh 1 uses u8 indices
(fmt 06 02 02 02: position, colour, texcoord). Node = sphere (vertex origin), link pointer, 0, second sphere + position
array pointer + the 32-byte mesh header.
usage: mk_toshi_terrain.py OUTDIR  ->  OUTDIR/Data/Terrain/Area/Test_L0Mod0.trb"""
import os, struct, sys
out = sys.argv[1]
os.makedirs(out + '/Data/Terrain/Area', exist_ok=True)
B = lambda *a: struct.pack('>%dI' % len(a), *a)
F = lambda *a: struct.pack('>%df' % len(a), *a)
S = bytearray(0x800)
def put(off, b): S[off:off + len(b)] = b

HD, LOD, MT, CO, DB, TAB = 0x00, 0x0c, 0x40, 0x170, 0x178, 0x180
put(HD, B(1) + F(100.0) + B(LOD))
put(LOD, B(2, 0, 2, 0) + F(0, 0, 0, 50))              # 2 meshes, world shader
put(MT, B(0, 0, 1, 296) + b'terr'.ljust(104, b'\0') + b'World\\terr.tga'.ljust(192, b'\0'))
N0, N1 = 0x200, 0x300
put(TAB, B(2, TAB + 12, 0) + B(N0, N1))   # {count, ptr to the array, culling tree ptr}, array
MATN, DL0, DL1 = 0x3f0, 0x600, 0x640
put(MATN, b'terr\0')

def node(base, org, pos, uv, col, dl, dsz, cnt, fmt):
    put(base, F(*org, 5.0))                               # bounding sphere
    put(base + 0x10, B(0x100))                            # link pointer, 0, then the second sphere (this layout: A at +0x28)
    put(base + 0x28, B(pos))                              # position array (word before the header)
    put(base + 0x2c, B(uv, col, max(pos, uv, col), dl, dsz, MATN, cnt) + bytes(fmt))   # word 3 = end of the node's arrays

# mesh 0: direct vertices, origin (10, 20, 0) in the Z-up source frame
pos_end = N0 + 0x80
node(N0, (10.0, 20.0, 0.0), pos_end, pos_end, pos_end, DL0, 64, 3, (6, 1, 1, 1))
dl = bytes([0x98]) + struct.pack('>H', 3)
for (x, y, z), c, (u, v) in (((0, 0, 0), 0xf800, (0, 0)), ((64, 0, 0), 0x07e0, (256, 0)), ((0, 64, 0), 0x001f, (0, 256))):
    dl += struct.pack('>3hH2h', x, y, z, c, u, v)
put(DL0, dl)

# mesh 1: indexed, origin (0, 0, 5)
A1, U1, C1 = N1 + 0x60, N1 + 0x80, N1 + 0xa0
node(N1, (0.0, 0.0, 5.0), A1, U1, C1, DL1, 32, 3, (6, 2, 2, 2))
put(A1, struct.pack('>9h', 0, 0, 0, 64, 0, 0, 0, 64, 0))
put(U1, struct.pack('>6h', 0, 0, 256, 0, 0, 256))
put(C1, struct.pack('>3H', 0xf800, 0x07e0, 0x001f))
put(DL1, bytes([0x98]) + struct.pack('>H', 3) + bytes([0, 0, 0, 1, 1, 1, 2, 2, 2]))

names = [(b'Header', HD), (b'Materials', MT), (b'Collision', CO), (b'Database', DB)]
symb = B(len(names)); nm = b''; ents = b''
for n, off in names:
    ents += struct.pack('>HHII', 0, len(nm), 0, off); nm += n + b'\0'
symb += ents + nm

def hunk(tag, pl): return tag + B(len(pl)) + pl + bytes(-len(pl) & 3)
body = b'FBRT' + hunk(b'XRDH', bytes(24)) + hunk(b'TCES', bytes(S)) + hunk(b'BMYS', symb)
open(out + '/Data/Terrain/Area/Test_L0Mod0.trb', 'wb').write(b'TSFB' + B(len(body)) + body + bytes(1024))
