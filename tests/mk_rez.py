#!/usr/bin/env python3
"""Synthetic Humongous Resource.rez (see project/src/lib-rez.h): one group with
a compressed 8x4-ish C8 texture (8x8) a DSP-ADPCM sound and a one-triangle model with an animation.
usage: mk_rez.py OUTDIR -> OUTDIR/Resource.rez"""
import os, struct, sys
out = sys.argv[1]
os.makedirs(out, exist_ok=True)

tex = bytearray(0x80)
struct.pack_into('>4H', tex, 0, 8, 8, 8, 9)
tex += bytes(range(64))
tex += b''.join(struct.pack('>H', 0x8000 | ((i & 31) << 10) | ((i & 31) << 5) | (i & 31)) for i in range(256))

snd = bytearray(0x80)
struct.pack_into('>4I', snd, 0, 6, 22050, 0, 16)
struct.pack_into('>16h', snd, 0x44, *([0x800, 0] * 8))
snd += bytes([0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77] * 2)

# model object (see lib-rez.h): one triangle list, 3 positions / 1 normal / 3 uvs
mesh = bytearray(0x14c)
struct.pack_into('>3I', mesh, 0, 0x7843, 0, 16)                 # flags, -, display list bytes
struct.pack_into('>I', mesh, 0x10, 3)                             # positions
struct.pack_into('>I', mesh, 0x20, 1)                             # normals
struct.pack_into('>2I', mesh, 0x28, 3, 1)                         # uvs, batches
struct.pack_into('>3f', mesh, 0x48, 0, 0, 0)                      # rest position
struct.pack_into('>4f', mesh, 0x54, 0, 0, 0, 1)                   # rest quaternion
struct.pack_into('>3f', mesh, 0x64, 1, 1, 1)                      # rest scale
mesh[0xe0:0xec] = bytes([0x90, 0, 3, 0, 0, 0, 1, 0, 1, 2, 0, 2])
struct.pack_into('>9f', mesh, 0xf0, 0, 0, 0, 1, 0, 0, 0, 1, 0)
struct.pack_into('>3f', mesh, 0x114, 0, 0, 1)
struct.pack_into('>6f', mesh, 0x120, 0, 0, 1, 0, 0, 1)
struct.pack_into('>5I', mesh, 0x138, 0, 0, 0, 12, 0)              # batch: texture, -, offset, length

# object animation: one node, two frames
anim = bytearray(24)
struct.pack_into('>2If', anim, 0, 0, 2, 1.0)
anim += struct.pack('>10f', 0, 0, 0, 0, 0, 0, 1, 1, 1, 1)
anim += struct.pack('>10f', 1, 2, 3, 0, 0, 0, 1, 1, 1, 1)

def pack(data):
    # literal-only compression: u32 unpacked size, then runs of <= 127 bytes
    p = struct.pack('>I', len(data))
    for i in range(0, len(data), 127):
        chunk = data[i:i + 127]
        p += bytes([0x80 | len(chunk)]) + bytes(chunk)
    return p

ptex, psnd, pmesh, panim = pack(tex), pack(snd), pack(mesh), pack(anim)
table = struct.pack('>4I', 4, 0, 0, 0)
table += struct.pack('>IIIhHII', 0, len(ptex), len(tex), 73, 1, 0, 0)
table += struct.pack('>IIIhHII', len(ptex), len(psnd), len(snd), 76, 1, 0, 0)
table += struct.pack('>IIIhHII', len(ptex) + len(psnd), len(pmesh), len(mesh), 75, 1, 0, 0)
table += struct.pack('>IIIhHII', len(ptex) + len(psnd) + len(pmesh), len(panim), len(anim), 86, 1, 0, 0)
group = ptex + psnd + pmesh + panim + table
data = group + bytes(-len(group) % 2048)
slot = struct.pack('>IIIhHII', 0, len(group), len(tex) + len(snd) + len(mesh) + len(anim), 1, len(table), 0, 0)
data += slot + bytes(2048 - len(slot))
data += struct.pack('>3I', 0, 0, 1) + bytes(2048 - 12)
open(out + '/Resource.rez', 'wb').write(data)
