#!/usr/bin/env python3
# Synthetic Reflections 04SW stereo (.xa) with one full 0x8000 block and an 8-byte tail per channel as t.xa.
import struct, sys, os
d = sys.argv[1]
il = 0x8000
frames = il // 8 + 1
f = bytearray(0x800)
f[0:4] = b'04SW'
for c in range(2):
    o = 4 + 0x60 * c
    struct.pack_into('>III', f, o, frames * 14, frames * 16, 32000)
    struct.pack_into('>III', f, o + 0x10, 2, frames * 16 - 1, 2)
    struct.pack_into('>I', f, o + 0x1c, 0x030600d6)
    f[o + 0x3c:o + 0x44] = bytes.fromhex('a4f5120020f71200')
struct.pack_into('>I', f, 0xc4, 0x800)
f += bytes([0x11]) * il + bytes([0x22]) * il + bytes([0x13]) * 8 + bytes([0x24]) * 8
open(os.path.join(d, 't.xa'), 'wb').write(bytes(f))
