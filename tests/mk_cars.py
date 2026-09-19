#!/usr/bin/env python3
"""Synthetic Rainbow Studios (Disney-Pixar Cars) assets, see
project/src/lib-rainbow.h: a CI8 4x4 .gct texture (pixel 0,0 opaque red), a
.gcm naming it and a .gcg with one textured quad strip.
usage: mk_cars.py OUTDIR -> OUTDIR/Tex/red.gct, red.gcm, Geo/quad.gcg"""
import os, struct, sys

def be(*v): return struct.pack('>%dI' % len(v), *v)

out = sys.argv[1]
os.makedirs(out + '/Tex', exist_ok=True)
os.makedirs(out + '/Geo', exist_ok=True)

# CI8: palette entry 0 = opaque red in RGB5A3 (0x8000 | r4<<... -> 0xFF00 style: a=7 -> 0x7F00)
pal = bytearray(512)
pal[0:2] = struct.pack('>H', 0x7F00)   # a3=7 r4=15 g4=0 b4=0
img = bytes(4 * 8)                      # one 8x4 CI8 tile of index 0 (width 4 pads to 8)
gct = be(2, 58, 256) + bytes(pal) + be(1, 4, 4) + be(4, 4, len(img)) + img
open(out + '/Tex/red.gct', 'wb').write(gct)
open(out + '/Tex/red.gcm', 'w').write('[General]\r\nTotalShaderPasses=1\r\n[ShaderPass_1]\r\nTextureMap_1=red\r\n')

# gcg: one material "red", one strip of 4 vertices (pos idx8, colour idx8, uv idx8)
hdr = bytearray(0xf0)
hdr[0:12] = b'gcg\0' + be(5, 1)
hdr[0x0c:0x0c + 4] = b'quad'
for i in range(4):
    hdr[0x8c + i * 20:0x8c + i * 20 + 4] = struct.pack('>f', 1.0)
hdr[0xe8:0xf0] = be(0xffffffff, 1)
mat = bytearray(0x40); mat[0:3] = b'red'
pos = b''.join(struct.pack('>3h', *p) for p in [(0, 0, 0), (16, 0, 0), (0, 16, 0), (16, 16, 0)])
clr = b'\xff\xff\xff\xff' * 4
uv = b''.join(struct.pack('>2h', *p) for p in [(0, 0), (256, 0), (0, 256), (256, 256)])
dl = bytes([0x98, 0, 4]) + bytes(sum(([i, i, i] for i in range(4)), []))
strip = bytes([2]) + bytes([2, 3, 4]) + bytes([2, 5]) + bytes([2, 3, 8]) \
    + struct.pack('>HB', 4, 6) + struct.pack('>HB', 4, 4) + struct.pack('>HB', 4, 4) \
    + be(len(dl)) + pos + clr + uv + dl
gcg = bytes(hdr) + bytes(mat) + be(1, 0x7f7fffff, 1) + be(0) + strip
open(out + '/Geo/quad.gcg', 'wb').write(gcg)
