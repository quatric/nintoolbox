#!/usr/bin/env python3
# Synthetic Infernal ITL stereo DSP (MX vs. ATV Untamed, interleave 0x10000) as t.itl.
import struct, sys, os
d = sys.argv[1]
il = 0x10000
r = 24
per = il - 0x60 + r
frames = per // 8
def hdr():
    h = bytearray(0x60)
    struct.pack_into('>III', h, 0, frames * 14, frames * 16, 44100)
    struct.pack_into('>III', h, 0x10, 2, frames * 16 - 1, 2)
    struct.pack_into('>I', h, 0x1c, 0x030600d6)
    return bytes(h)
f = hdr() + bytes([0x11]) * (il - 0x60) + hdr() + bytes([0x22]) * (il - 0x60) + bytes([0x13]) * r + bytes([0x24]) * r
open(os.path.join(d, 't.itl'), 'wb').write(f)
