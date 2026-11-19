#!/usr/bin/env python3
# Synthetic Game Freak NTBG (.5bg): 4 bpp, 16-colour palette, 32x24 tiles; tile 1 = solid colour 2,
# top-left map entry = tile 1 with sub-palette 0, top-right = tile 1 flipped with sub-palette 1 (n/a: 16 colours -> clear).
import struct, sys, os
d = sys.argv[1]
pal = [0, 0x001f, 0x03e0, 0x7c00] + [0] * 12  # transparent, red, green, blue (BGR555)
palt = b'PALT' + struct.pack('<II', 8 + 4 + 32, 16) + struct.pack('<16H', *pal)
palt = palt[:4] + struct.pack('<I', 12 + 32) + palt[8:]
scr = bytearray(2 * 32 * 24)
struct.pack_into('<H', scr, 0, 1)            # (0,0) -> tile 1
struct.pack_into('<H', scr, 2, 1 | 1 << 10)  # (1,0) -> tile 1, h-flip
tiles = bytes(32) + bytes([0x22] * 32) + bytes([0x31] * 32)  # tile 0 empty, tile 1 colour 2, tile 2 colour 1/3 stripes
bgdt_body = struct.pack('<BBHIHHHHI', 0, 0xff, 2, len(scr), 32, 24, 32, 24, len(tiles)) + bytes(scr) + tiles
bgdt = b'BGDT' + struct.pack('<I', 8 + len(bgdt_body)) + bgdt_body
body = palt + bgdt
out = b'NTBG' + struct.pack('<HHIHH', 0xfeff, 0x100, 16 + len(body), 16, 2) + body
open(os.path.join(d, 't.5bg'), 'wb').write(out)
