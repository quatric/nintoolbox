#!/usr/bin/env python3
"""Synthetic Humongous Resource.rez (see project/src/lib-rez.h): one group with
one compressed 8x8 C8 texture. usage: mk_rez.py OUTDIR -> OUTDIR/Resource.rez"""
import os, struct, sys
out = sys.argv[1]
os.makedirs(out, exist_ok=True)

tex = bytearray(0x80)
struct.pack_into('>4H', tex, 0, 8, 8, 8, 9)
tex += bytes(range(64))
tex += b''.join(struct.pack('>H', 0x8000 | (i << 10) | (i << 5) | i) for i in range(256))
# literal-only compression: u32 unpacked size, then runs of <= 127 bytes
packed = struct.pack('>I', len(tex))
for i in range(0, len(tex), 127):
    chunk = tex[i:i + 127]
    packed += bytes([0x80 | len(chunk)]) + bytes(chunk)

table = struct.pack('>4I', 1, 0, 0, 0) + struct.pack('>IIIhHII', 0, len(packed), len(tex), 73, 1, 0, 0)
group = packed + table
data = group + bytes(-len(group) % 2048)
slot = struct.pack('>IIIhHII', 0, len(group), len(tex), 1, len(table), 0, 0)
data += slot + bytes(2048 - len(slot))
data += struct.pack('>3I', 0, 0, 1) + bytes(2048 - 12)
open(out + '/Resource.rez', 'wb').write(data)
