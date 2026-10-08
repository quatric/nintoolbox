#!/usr/bin/env python3
# Synthetic Messiah "2df" sprite sheet with one 8x8 RGBA8 texture, written as t.spr.
import struct, sys, os
out = sys.argv[1]
w = h = 8
pix = b''
for by in range(0, h, 4):
    for bx in range(0, w, 4):
        ar = b''.join(bytes([255, 200]) for _ in range(16))
        gb = b''.join(bytes([100, 50]) for _ in range(16))
        pix += ar + gb
hdr = bytearray(0x200)
hdr[0:4] = b'2df\0'
struct.pack_into('<II', hdr, 4, 11, 0x3c)
struct.pack_into('<I', hdr, 0x108, 1)
struct.pack_into('<8I', hdr, 0x1e0, 0x10001, 2, len(pix) + 0x20, w, h, 0, 74, len(pix) + 8)
open(os.path.join(out, 't.spr'), 'wb').write(bytes(hdr) + pix + b'\0' * 8)
# version-10 single-texture sheet (Pool Hall Pro): 8x8 CMPR, data at 0x1f8, fmt 0x1a
w = h = 8
hdr = bytearray(0x1f8)
hdr[0:4] = b'2df\0'
struct.pack_into('<II', hdr, 4, 10, 0x2c)
struct.pack_into('<I', hdr, 0x108, 1)
struct.pack_into('<8I', hdr, 0x1d0, 0x10001, 2, w * h // 2 + 0x20, w, h, 0, 0x1a, w * h // 2 + 8)
blk = struct.pack('>HH', 0xf800, 0x001f) + bytes([0x1b] * 4)
open(os.path.join(out, 'v10.spr'), 'wb').write(bytes(hdr) + blk * 4)
