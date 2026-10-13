#!/usr/bin/env python3
# Synthetic Overkill-style Blitz texture: 8x8 RGB5A3 (format 24), all red; named t.bltex
import struct, sys, os
d = sys.argv[1]
w = h = 8
pix = struct.pack('>H', 0xfc00) * (w * h)
size = 0x60 + len(pix)
hdr = bytearray(0x60)
hdr[12:16] = struct.pack('>I', size)
hdr[0x20:0x28] = struct.pack('>4H', w, h, 24, 16)
hdr[0x28] = 1
open(os.path.join(d, 't.bltex'), 'wb').write(bytes(hdr) + pix)
