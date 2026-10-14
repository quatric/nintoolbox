#!/usr/bin/env python3
# Synthetic Z-Axis ZSND GCUB t.zss: one mono DSP stream (0x60-byte header + 16 data bytes).
import struct, sys, os
data = bytes(range(1, 17))
nib = len(data) * 2
h = bytearray(0x60)
struct.pack_into('>IIIHHIII', h, 0, nib // 16 * 14, nib, 22050, 0, 0, 2, nib - 1, 2)
for i in range(16):
    struct.pack_into('>h', h, 0x1c + 2 * i, 0x100 * (i + 1))
b = bytearray(0x80)
b[0:8] = b'ZSNDGCUB'
struct.pack_into('>II', b, 8, 0x80 + 0x60 + len(data), 0x80)
struct.pack_into('>I', b, 0x1c, 1)
struct.pack_into('>I', b, 0x24, 0x40)                 # table 2: entries @0x1c, body @0x24
struct.pack_into('>I', b, 0x28, 1)
struct.pack_into('>I', b, 0x30, 0x58)                 # table 3: entries @0x28, body @0x30
struct.pack_into('>HHI', b, 0x40, 0, 0, 22050)         # layers @+2 (0 = mono), rate @+4
struct.pack_into('>III', b, 0x58, 0x80, 0x60 + len(data), 0x44535020)
open(os.path.join(sys.argv[1], 't.zss'), 'wb').write(bytes(b) + bytes(h) + data)
