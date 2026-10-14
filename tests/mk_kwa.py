#!/usr/bin/env python3
# Synthetic Knight Wars / Aragorn's Quest t.kwa: mono, interleave 0x100, one DSP header + 0x20 bytes of data.
import struct, sys, os
data = bytes(range(1, 0x21))
nib = len(data) * 2
b = bytearray(0x20 + 0x100)
struct.pack_into('>IIII', b, 0, 3, 1, len(b), 0x100)
h = bytearray(0x60)
struct.pack_into('>IIIHHIII', h, 0, nib // 16 * 14, nib, 32000, 0, 0, 2, nib - 1, 2)
for i in range(16):
    struct.pack_into('>h', h, 0x1c + 2 * i, 0x100 * (i + 1))
b[0x20:0x80] = h
b[0x80:0x80 + len(data)] = data
open(os.path.join(sys.argv[1], 't.kwa'), 'wb').write(b)
