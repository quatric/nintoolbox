#!/usr/bin/env python3
# Synthetic Capcom MT Framework Wii motion bank ("\0TML") with one motion of two tracks as t.lmt.
import struct, sys, os
d = sys.argv[1]
f = bytearray(0x200)
f[1:4] = b'TML'
f[5] = 0x38
struct.pack_into('>H', f, 6, 2)
struct.pack_into('>I', f, 8, 0)
struct.pack_into('>I', f, 12, 0x20)
struct.pack_into('>IIII', f, 0x20, 0x180, 2, 73, 0)
for i in range(2):
    struct.pack_into('>BBBBfII', f, 0x180 + i * 0x24, 11 + i, 0, i, 6 + i, 1.0, 0x18, 0x1d0)
open(os.path.join(d, 't.lmt'), 'wb').write(bytes(f))
