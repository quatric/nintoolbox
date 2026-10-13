#!/usr/bin/env python3
# Synthetic Sakura Wars HTEX: 16x16 8-bit PS2-swizzled texture; pixel (0,0) = red, (15,15) = blue
import struct, sys, os
d = sys.argv[1]
w = h = 16
lin = bytearray(w * h)
lin[0] = 1
lin[15 * w + 15] = 2
def swz_index(x, y):
    bl = (y & ~0xf) * w + (x & ~0xf) * 2
    sw = (((y + 2) >> 2) & 1) * 4
    py = (((y & ~3) >> 1) + (y & 1)) & 7
    cl = py * w * 2 + ((x + sw) & 7) * 4
    bn = ((y >> 1) & 1) + ((x >> 2) & 2)
    return bl + cl + bn
swz = bytearray(w * h)
seen = set()
for y in range(h):
    for x in range(w):
        i = swz_index(x, y)
        assert i not in seen
        seen.add(i)
        swz[i] = lin[y * w + x]
# palette entries 1 and 2 as stored (PS2 CLUT order only swaps 8..15 / 16..23 blocks, so 1 and 2 stay put)
pal = bytearray(0x400)
pal[4:8] = bytes([255, 0, 0, 0x80])
pal[8:12] = bytes([0, 0, 255, 0x80])
hdr = b'HTEX' + struct.pack('<IIi', 0x10420, 0x10, 0)[:12] + b'HTSF' + struct.pack('<IIi', 0x10410, 0x10, 0)[:12]
pvrt = b'PVRT' + struct.pack('<I', 8 + 0x400 + w * h) + bytes([9, 0x6c, 0, 0]) + struct.pack('<HH', w, h)
open(os.path.join(d, 't.htx'), 'wb').write(hdr + pvrt + bytes(pal) + bytes(swz))
