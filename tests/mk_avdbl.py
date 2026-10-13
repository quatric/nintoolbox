#!/usr/bin/env python3
# Synthetic Avalanche DBL (Disney's Bolt): one DB_TEXTURE_SET chunk with a 4x4 RGB565 texture (red,
# top-left blue) and an 8x4 C8 texture (palette index 1 = green; 16x16 RGB565 palette).
import struct, sys, os
d = sys.argv[1]
def rec(name, fmt, w, h, po, pal, pw, ph):
    return name.ljust(0x20, b'\0') + struct.pack('>IIIIHHIIIHHI', 0x8000 | fmt, 0, 0, po, w, h, 0, 0, pal, pw, ph, 0)[:0x28]
n = 2
recs_end = 0x18 + 0x48 * n
pal_off = recs_end
pix0 = pal_off + 512
pix1 = pix0 + 32
payload = bytearray(pix1 + 32)
payload[0:4] = struct.pack('>I', n)
payload[0x18:0x18 + 0x48] = rec(b'maps\\red.tga', 4, 4, 4, pix0, 0, 0, 0)
payload[0x18 + 0x48:0x18 + 0x90] = rec(b'maps\\green.tga', 9, 8, 4, pix1, pal_off, 16, 16)
for i in range(256):
    payload[pal_off + 2 * i:pal_off + 2 * i + 2] = struct.pack('>H', 0x07e0 if i == 1 else 0)
px = [0xf800] * 16
px[0] = 0x001f
payload[pix0:pix0 + 32] = b''.join(struct.pack('>H', v) for v in px)
payload[pix1:pix1 + 32] = bytes([1] * 32)
hdr = b'\x30\0\0\0' + bytes(4) + b'GCN\0' + bytes(4) + struct.pack('>I', 0x100) + bytes(12)
chunk = struct.pack('>HHIH', 0x82, 0xe, len(payload), 4) + bytes(0x40 - 10) + bytes(payload)
open(os.path.join(d, 't.dbl'), 'wb').write(hdr + chunk)
