#!/usr/bin/env python3
# Synthetic Kirby's Epic Yarn background tile set t.bgst3: two 8x8 CMPR tiles (one empty, one coloured) and a slot table.
import struct, sys, os
w = h = 8
tsz = w * h // 2
empty = struct.pack('>II', 1, 0xffffffff) * (tsz // 8)
col = struct.pack('>HHI', 0xf800, 0x001f, 0x1b1b1b1b) * (tsz // 8)
slot = struct.pack('>8H', 1, 0, 0, 0, 1, 0xffff, 0xffff, 0xffff)
hdr = struct.pack('>12I', 0x42475354, 0x10, w, h, 6, 3, 2, 0x1000000, 0x1000000, 0, 0x40, 0x50)
body = hdr.ljust(0x40, b'\0') + slot + empty + col
open(os.path.join(sys.argv[1], 't.bgst3'), 'wb').write(body)
