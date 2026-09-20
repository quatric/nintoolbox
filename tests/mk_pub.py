#!/usr/bin/env python3
"""Synthetic Atomic Planet PUB package (see project/src/lib-pub.h): one 8x4 I8
texture. usage: mk_pub.py OUTDIR -> OUTDIR/T_WII.PUB"""
import os, struct, sys
out = sys.argv[1]
os.makedirs(out, exist_ok=True)

obj = bytearray(0x60)
struct.pack_into('>I', obj, 0x18, 0)                  # no palette
obj[0x20:0x24] = bytes([1, 1, 0, 0])                  # 1 mip, GX I8
struct.pack_into('>IIHH', obj, 0x24, 0x40, 32, 4, 8)  # offset, size, height, width
obj[0x40:0x60] = bytes(range(0, 256, 8))
tbl_off = 0x40 + len(obj)
entry = struct.pack('>IIHHIIII', 0xdeadbeef, 0x40, 1, 1, len(obj), len(obj), 0, 0)
body = bytes(obj) + entry
total = 0x40 + len(body) + 0x20
hdr = struct.pack('>8I', 1, 1, total - 0x20, 0, 0, 0x1c, 0x1c, 0)
sec = struct.pack('>8I', 1, 1, tbl_off, 0, 0, 28, 28, 0)
open(out + '/T_WII.PUB', 'wb').write(hdr + sec + body + bytes(0x20))
