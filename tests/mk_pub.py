#!/usr/bin/env python3
"""Synthetic Atomic Planet PUB package (see project/src/lib-pub.h): one 8x4 I8
texture and one single-triangle mesh. usage: mk_pub.py OUTDIR -> OUTDIR/T_WII.PUB"""
import os, struct, sys
out = sys.argv[1]
os.makedirs(out, exist_ok=True)

obj = bytearray(0x60)
struct.pack_into('>I', obj, 0x18, 0)                  # no palette
obj[0x20:0x24] = bytes([1, 1, 0, 0])                  # 1 mip, GX I8
struct.pack_into('>IIHH', obj, 0x24, 0x40, 32, 4, 8)  # offset, size, height, width
obj[0x40:0x60] = bytes(range(0, 256, 8))

# mesh object: one part, position slot only, one triangle list
mesh = bytearray(0x180)
struct.pack_into('>6f', mesh, 0, 0, 0, 0, 1, 1, 0)                 # bounding box
struct.pack_into('>I', mesh, 0x70, 0x74)
struct.pack_into('>5I', mesh, 0x74, 0x10001, 0x8c, 0x90, 0x94, 1 << 16)
struct.pack_into('>3I', mesh, 0x8c, 0x98, 0xa8, 0xc8)
struct.pack_into('>4I', mesh, 0x98, 0x10001, 0x120, 0x20, 3)       # display list
struct.pack_into('>4I', mesh, 0xa8, 0x10001, 0, 0, 0xdeadbeef)     # texture hash
struct.pack_into('>3I', mesh, 0xc8, 0x10001, 0xd4, 6 << 16)        # 6 slots at +12
struct.pack_into('>6I', mesh, 0xd4, 0xec, 0x110, 0x110, 0x110, 0x110, 0x110)
struct.pack_into('>3I', mesh, 0xec, 0x10001, 0xf8, 1 << 16)        # slot 0
struct.pack_into('>I', mesh, 0xf8, 0x100)                          # its record list
struct.pack_into('>2IH2B4x', mesh, 0x100, 0x10001, 0x140, 3, 0, 12)  # data, count, off, stride
mesh[0x100 + 12] = 12                                              # attribute size
struct.pack_into('>3I', mesh, 0x110, 0x10001, 0, 0)                # empty slot
mesh[0x120:0x129] = bytes([0x90, 0, 3, 0, 0, 0, 1, 0, 2])
struct.pack_into('>9f', mesh, 0x140, 0, 0, 0, 1, 0, 0, 0, 1, 0)

objs = [bytes(obj), bytes(mesh)]
offs, pos = [], 0x40
for o in objs:
    offs.append(pos)
    pos += len(o)
tbl_off = pos
kinds = [(0xdeadbeef, 1), (0xcafef00d, 8)]
table = b''.join(struct.pack('>IIHHIIII', h, off, k, 1, len(o), len(o), 0, 0)
                 for (h, k), off, o in zip(kinds, offs, objs))
total = tbl_off + len(table) + 0x20
hdr = struct.pack('>8I', 1, 1, total - 0x20, 0, 0, 0x1c, 0x1c, 0)
sec = struct.pack('>8I', 1, len(objs), tbl_off, 0, 0, 28 * len(objs), 28 * len(objs), 0)
open(out + '/T_WII.PUB', 'wb').write(hdr + sec + b''.join(objs) + table + bytes(0x20))
