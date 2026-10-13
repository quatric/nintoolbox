#!/usr/bin/env python3
# Synthetic Arc Rise Fantasia WTMD: 4x4 RGBA8 (GX format 6), all red
import struct, sys, os
d = sys.argv[1]
w = h = 4
tile = bytes([255, 255] * 16) + bytes([0, 0] * 16)   # AR pairs, then GB pairs
hdr = b'WTMD' + struct.pack('>IHHBBBBII', 0x20, w, h, 6, 0, 1, 0, 0x20, 0x20000) + bytes(8)
open(os.path.join(d, 't.wtm'), 'wb').write(hdr + tile)
