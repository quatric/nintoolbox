#!/usr/bin/env python3
# Synthetic tri-Crescendo CAF (Fragile Dreams): two blocks, stereo, extensionless "FILE_1" ... written as t.caf.
import struct, sys, os
d = sys.argv[1]
bs = 0x1000
def block(num, c0, c1, ps0, ps1):
    b = bytearray(bs)
    b[0:4] = b'CAF '
    struct.pack_into('>IIII', b, 4, bs, num, 0, 0x800)
    struct.pack_into('>IIII', b, 0x14, len(c0), 0x800 + len(c0), len(c1), 0)
    struct.pack_into('>I', b, 0x20, 0xffff)
    for c, ps in ((0, ps0), (1, ps1)):
        o = 0x28 + 0x2c * c
        struct.pack_into('>II', b, o, 0, ps)
        struct.pack_into('>16h', b, o + 0xc, *[0x100 * (i + 1) for i in range(16)])
    b[0x800:0x800 + len(c0)] = c0
    o1 = 0x800 + len(c0)
    b[o1:o1 + len(c1)] = c1
    return bytes(b)
f = block(0, bytes([0x11]) * 0x400, bytes([0x21]) * 0x400, 0x11, 0x21) + block(1, bytes([0x12]) * 0x200, bytes([0x22]) * 0x200, 0x12, 0x22)
open(os.path.join(d, 't.caf'), 'wb').write(f)
