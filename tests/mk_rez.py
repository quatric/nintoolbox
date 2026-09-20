#!/usr/bin/env python3
"""Synthetic Humongous Resource.rez (see project/src/lib-rez.h): one group with
a compressed 8x4-ish C8 texture (8x8) a DSP-ADPCM sound, a one-triangle model with an animation, a skinned model and a motion.
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
struct.pack_into('>I', mesh, 0x28, 3)                             # uvs
struct.pack_into('>I', mesh, 0x30, 1)                             # batches
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

# skinned model: same triangle, all weighted to bone 0 of a 2-bone skeleton
skin = bytearray(0x1e0)
struct.pack_into('>3I', skin, 0, 0x7943, 0, 16)
struct.pack_into('>I', skin, 0x10, 3)
struct.pack_into('>I', skin, 0x20, 1)
struct.pack_into('>I', skin, 0x28, 3)
struct.pack_into('>I', skin, 0x30, 1)
struct.pack_into('>I', skin, 0x44, 1)                              # skeleton follows
struct.pack_into('>4f', skin, 0x54, 0, 0, 0, 1)
struct.pack_into('>3f', skin, 0x64, 1, 1, 1)
skin[0xe0:0xec] = bytes([0x90, 0, 3, 0, 0, 0, 1, 0, 1, 2, 0, 2])
struct.pack_into('>9f', skin, 0xf0, 0, 0, 0, 1, 0, 0, 0, 1, 0)
struct.pack_into('>3f', skin, 0x114, 0, 0, 1)
for i in range(3):                                                 # bone[4], weight[3]
    struct.pack_into('>4B3f', skin, 0x120 + 16 * i, 0, 0xff, 0xff, 0, 1.0, 0, 0)
struct.pack_into('>6f', skin, 0x150, 0, 0, 1, 0, 0, 1)
struct.pack_into('>5I', skin, 0x168, 0, 0, 0, 12, 0)
struct.pack_into('>I', skin, 0x17c, 2)                             # skeleton: 2 bones, identity tables
struct.pack_into('>4I', skin, 0x18c, 0, 4, 0, 4)
struct.pack_into('>3fI', skin, 0x19c, 0, 0, 10, 0)                 # bone 0, child count at +16
struct.pack_into('>I', skin, 0x19c + 16, 1)
struct.pack_into('>3f', skin, 0x1bc, 0, 0, 5)                      # bone 1

# motion: 2 frames of the 2-bone skeleton
motion = bytearray(0x14)
struct.pack_into('>2IfI', motion, 0, 2, 2, 1.0, 28)
for f in range(2):
    motion += struct.pack('>3f', 0, 0, f) + struct.pack('>8h', 0, 0, 0, 32767, 0, 0, 0, 32767)

def pack(data):
    # literal-only compression: u32 unpacked size, then runs of <= 127 bytes
    p = struct.pack('>I', len(data))
    for i in range(0, len(data), 127):
        chunk = data[i:i + 127]
        p += bytes([0x80 | len(chunk)]) + bytes(chunk)
    return p

ptex, psnd, pmesh, panim, pskin = pack(tex), pack(snd), pack(mesh), pack(anim), pack(skin)
table = struct.pack('>4I', 5, 0, 0, 0)
table += struct.pack('>IIIhHII', 0, len(ptex), len(tex), 73, 1, 0, 0)
table += struct.pack('>IIIhHII', len(ptex), len(psnd), len(snd), 76, 1, 0, 0)
table += struct.pack('>IIIhHII', len(ptex) + len(psnd), len(pmesh), len(mesh), 75, 1, 0, 0)
table += struct.pack('>IIIhHII', len(ptex) + len(psnd) + len(pmesh), len(panim), len(anim), 86, 1, 0, 0)
table += struct.pack('>IIIhHII', len(ptex) + len(psnd) + len(pmesh) + len(panim), len(pskin), len(skin), 75, 1, 0, 0)
group = ptex + psnd + pmesh + panim + pskin + table
data = group + bytes(-len(group) % 4)
motion_off = len(data)
data += bytes(motion) + bytes(-(len(data) + len(motion)) % 2048)
slots = struct.pack('>IIIhHII', 0, len(group), len(tex) + len(snd) + len(mesh) + len(anim) + len(skin), 1, len(table), 0, 0)
slots += struct.pack('>IIIhHII', motion_off, len(motion), len(motion), 7, 0, 0, 0)
data += slots + bytes(2048 - len(slots))
data += struct.pack('>3I', 0, 0, 2) + bytes(2048 - 12)
open(out + '/Resource.rez', 'wb').write(data)
