#!/usr/bin/env python3
"""Synthetic Toshi model .trb (Nickelodeon Barnyard Wii; see project/src/lib-toshi.h):
one bone, one material, one skin-shader mesh with a 4-vertex strip (2 triangles).
usage: mk_toshi_model.py OUTDIR  ->  OUTDIR/Data/Models/Quad.trb"""
import os, struct, sys
out = sys.argv[1]
os.makedirs(out + '/Data/Models', exist_ok=True)
B = lambda *a: struct.pack('>%dI' % len(a), *a)
F = lambda *a: struct.pack('>%df' % len(a), *a)

S = bytearray(0x800)
def put(off, b): S[off:off + len(b)] = b

# Skeleton at 0x38: u16 bone count, bones at +0x48 (192 bytes: quaternion, 2 matrices,
# name length + 31 name bytes, s16 parent, position)
SK = 0x38
put(SK, struct.pack('>H', 1))
bone = SK + 0x48
ident = F(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
put(bone, F(0, 0, 0, 1) + ident + ident + bytes([4]) + b'root' + bytes(27) + struct.pack('>h', -1)
    + bytes(2) + F(0, 0, 0))
# Materials at 0x140: {0, 0, count, size}, then name[104] + texture[192]
MT = 0x140
put(MT, B(0, 0, 1, 296) + b'quadmat'.ljust(104, b'\0') + b'Test\\tex.tga'.ljust(192, b'\0'))
# Header / LOD
HD = 0x280; LOD = 0x290
put(HD, B(1) + F(10.0) + B(LOD))
put(LOD, B(1, 0, 0, 0) + F(0, 0, 0, 2))          # skin shader
# arrays: positions s16 x3 (frac 8), normals s8 x3, texcoords s16 x2 (frac 8)
POS, NRM, UV, SUB, DL, SKIN, MESH, MATN = 0x300, 0x340, 0x360, 0x380, 0x3a0, 0x400, 0x440, 0x470
put(POS, struct.pack('>12h', 0, 0, 0, 256, 0, 0, 0, 256, 0, 256, 256, 0))
put(NRM, struct.pack('>12b', 0, 0, 64, 0, 0, 64, 0, 0, 64, 0, 0, 64))
put(UV, struct.pack('>8h', 0, 0, 256, 0, 0, 256, 256, 256))
put(MATN, b'quadmat\0')
# submesh: dl ptr, dl size, vertex count, bones[12] (slot 0 -> skin entry 0)
put(SUB, B(DL, 32, 4) + bytes([0]) + bytes([0xff] * 11))
# DL: tristrip of 4 vertices {slot*3, pos u16, nrm u16, uv u16}
dl = bytes([0x98]) + struct.pack('>H', 4)
for i in range(4):
    dl += bytes([0]) + struct.pack('>3H', i, i, i)
put(DL, dl)
# skin table {count, ptr}; entry {n, bone[3], weight[3]}
put(SKIN, B(1, SKIN + 8) + bytes([1, 0, 0, 0]) + F(1, 0, 0))
# mesh header: pos, nrm, uv, sub table, sub count, material, skin table, fmt
put(MESH, B(POS, NRM, UV, SUB, 1, MATN, SKIN) + bytes([8, 3, 3, 3]))

names = [(b'Skeleton', SK), (b'Materials', MT), (b'Header', HD), (b'LOD0_Mesh_0', MESH)]
symb = B(len(names)); nm = b''; ents = b''
for n, off in names:
    ents += struct.pack('>HHII', 0, len(nm), 0, off); nm += n + b'\0'
symb += ents + nm

def hunk(tag, pl): return tag + B(len(pl)) + pl + bytes(-len(pl) & 3)
body = b'FBRT' + hunk(b'XRDH', bytes(24)) + hunk(b'TCES', bytes(S)) + hunk(b'BMYS', symb)
open(out + '/Data/Models/Quad.trb', 'wb').write(b'TSFB' + B(len(body)) + body + bytes(1024))
