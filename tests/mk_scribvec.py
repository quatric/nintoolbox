#!/usr/bin/env python3
# Synthetic Scribblenauts vector mesh (float points): one triangle, one colour.
import struct, sys, os
out = sys.argv[1]
b = struct.pack('<BHHHBH', 0x02, 8, 8, 1, 1, 3)
for x, y in ((0, 0), (1, 0), (0, 1)):
    b += struct.pack('<ffH', x, y, 0)
b += struct.pack('<H3H', 3, 0, 1, 2)
b += struct.pack('<HHHH', 1, 0, 0, 3)          # one mesh group: id 0, indices 0..3
b += struct.pack('<HH', 0, 0) * 2  # two empty outline tables
b += struct.pack('<H4BH', 1, 0x10, 0x20, 0x30, 0xff, 0)
open(os.path.join(out, 'v.bin'), 'wb').write(b)
