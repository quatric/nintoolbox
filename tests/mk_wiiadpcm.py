#!/usr/bin/env python3
# Synthetic Exient WIIADPCM stereo (.adpcm, v1 layout, interleave 0x200) as t.adpcm.
import struct, sys, os
d = sys.argv[1]
il = 0x200
frames = 100
nib = frames * 16
def hdr(fill):
    h = bytearray(0x60)
    struct.pack_into('>III', h, 0, frames * 14, nib, 32000)
    struct.pack_into('>I', h, 0x1c, 0x030600d6)
    return bytes(h)
per = frames * 8
first = il - 0x10 - 0x60
f = bytearray(il * 6)
f[0:8] = b'WIIADPCM'
struct.pack_into('>I', f, 8, 0x10 + il)
struct.pack_into('>I', f, 0xc, 0x10 + il)
d0 = bytes([0x11]) * per
d1 = bytes([0x22]) * per
f[0x10:0x70] = hdr(0)
f[0x70:0x70 + first] = d0[:first]
f[il + 0x10:il + 0x70] = hdr(1)
f[il + 0x70:il + 0x70 + first] = d1[:first]
f[2 * il:2 * il + per - first] = d0[first:]
f[3 * il:3 * il + per - first] = d1[first:]
open(os.path.join(d, 't.adpcm'), 'wb').write(bytes(f[:3 * il + per - first + 8]))
