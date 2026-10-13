#!/usr/bin/env python3
# Synthetic PlatinumGames IDD container (MadWorld): one TEX section with two textures, a 4x4 RGB5A3
# (t.idd.00.png: top-left red, the rest green, no palette) and an 8x8 C4 image using a 16 entry RGB565
# palette (t.idd.01.png: all pixels index 1 = blue).
import struct, sys, os
d = sys.argv[1]
def w(*a): return struct.pack('>%dI' % len(a), *a)
body = bytearray(0x400)
body[0:12] = w(0x20af30, 2, 0xc)
# pairs (texture record, palette header); texture 0 has no palette
body[0xc:0x14] = w(0x200, 0)
body[0x14:0x1c] = w(0x224, 0x30)
body[0x30:0x3c] = w(16 << 16, 1, 0x80)
for i in range(16):
    body[0x80 + 2 * i:0x82 + 2 * i] = struct.pack('>H', 0x001f if i == 1 else 0)
body[0x200:0x20c] = struct.pack('>HH', 4, 4) + w(5, 0x300)[0:8]
body[0x224:0x230] = struct.pack('>HH', 8, 8) + w(8, 0x340)[0:8]
px = [0xfc00] + [0x83e0] * 15
body[0x300:0x320] = b''.join(struct.pack('>H', v) for v in px)
body[0x340:0x360] = bytes([0x11] * 32)
sec = b'TEX\0' + w(1, 0, 0x10)
f = bytearray(0x20) + bytearray(sec) + body
f[0:4] = b'IDD\0'
f[4:8] = w(1)
f[8:16] = w(0, 0x20)
# TEX section at 0x20: tag, 1, 0, 0x10 -> body at 0x30
open(os.path.join(d, 't.idd'), 'wb').write(bytes(f))
