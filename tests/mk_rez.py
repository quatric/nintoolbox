#!/usr/bin/env python3
"""Synthetic Humongous Resource.rez (see project/src/lib-rez.h): one group with
a compressed 8x4-ish C8 texture (8x8) a DSP-ADPCM sound and a one-triangle mesh.
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

# geometry object: one triangle list, 3 positions / 1 normal / 3 uvs
mesh = bytearray(0x98)
tag = lambda off: 0x010b0000 | (off + 0x20)
struct.pack_into('>3I', mesh, 0, 0x7843, 0, 16)
struct.pack_into('>2I', mesh, 0x0c, tag(0x40), 3)
struct.pack_into('>I', mesh, 0x14, tag(0x50))
struct.pack_into('>2I', mesh, 0x20, 1, tag(0x74))
struct.pack_into('>2I', mesh, 0x28, 3, tag(0x80))
mesh[0x40:0x4c] = bytes([0x90, 0, 3, 0, 0, 0, 1, 0, 1, 2, 0, 2])
struct.pack_into('>9f', mesh, 0x50, 0, 0, 0, 1, 0, 0, 0, 1, 0)
struct.pack_into('>3f', mesh, 0x74, 0, 0, 1)
struct.pack_into('>6f', mesh, 0x80, 0, 0, 1, 0, 0, 1)

def pack(data):
    # literal-only compression: u32 unpacked size, then runs of <= 127 bytes
    p = struct.pack('>I', len(data))
    for i in range(0, len(data), 127):
        chunk = data[i:i + 127]
        p += bytes([0x80 | len(chunk)]) + bytes(chunk)
    return p

ptex, psnd, pmesh = pack(tex), pack(snd), pack(mesh)
table = struct.pack('>4I', 3, 0, 0, 0)
table += struct.pack('>IIIhHII', 0, len(ptex), len(tex), 73, 1, 0, 0)
table += struct.pack('>IIIhHII', len(ptex), len(psnd), len(snd), 76, 1, 0, 0)
table += struct.pack('>IIIhHII', len(ptex) + len(psnd), len(pmesh), len(mesh), 75, 1, 0, 0)
group = ptex + psnd + pmesh + table
data = group + bytes(-len(group) % 2048)
slot = struct.pack('>IIIhHII', 0, len(group), len(tex) + len(snd) + len(mesh), 1, len(table), 0, 0)
data += slot + bytes(2048 - len(slot))
data += struct.pack('>3I', 0, 0, 1) + bytes(2048 - 12)
open(out + '/Resource.rez', 'wb').write(data)
