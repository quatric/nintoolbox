#!/usr/bin/env python3
# Synthetic Speed 2 font cache t.dcf: 1024x16 IA4 atlas with a white block, 2 glyph records.
import struct, sys, os
d = sys.argv[1]
rows = 16
atlas = bytearray(1024 * rows)
for b in range(len(atlas) // 32):
    atlas[b * 32:(b + 1) * 32] = b'\xff' * 32 if b == 0 else bytes(32)
out = b'DC2\0' + struct.pack('>5I', 0, 0x800, 0x100, 0x40000, rows * 256) + struct.pack('>I', 0x40000)
out += bytes(atlas) + bytes(3)
for c, x in ((97, 0.0), (98, 12.0)):
    out += struct.pack('>H5f', c, x, 0.0, x + 11, 16.0, 7.0)
open(os.path.join(d, 't.dcf'), 'wb').write(out)
