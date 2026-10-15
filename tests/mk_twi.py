#!/usr/bin/env python3
# Synthetic Exient t.twi: 4x4 RGBA8 texture (one tile, AR then GB planes), one mip level.
import struct, sys, os
w = h = 4
tile = bytearray(64)
for k in range(16):
    tile[2 * k] = 0xff            # alpha
    tile[2 * k + 1] = 0x10 * k    # red
    tile[32 + 2 * k] = 0x20       # green
    tile[32 + 2 * k + 1] = 0x30   # blue
b = b'TWI\0' + struct.pack('>IIIIIII', w, h, 1, 3, 0, 0, len(tile)) + bytes(tile)
open(os.path.join(sys.argv[1], 't.twi'), 'wb').write(b)
