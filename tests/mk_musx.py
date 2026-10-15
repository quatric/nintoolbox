#!/usr/bin/env python3
# Synthetic Eurocom MUSX (Wii, SBNK form) t.000: one mono NGCA DSP sound of 16 data bytes (28 samples).
import struct, sys, os
data = bytes(range(1, 17))
b = bytearray(0x1000 + 0x40 + len(data))
b[0:4] = b'MUSX'
struct.pack_into('<I', b, 8, 10)
b[0x10:0x14] = b'WII_'
b[0x800:0x804] = b'SBNK'
struct.pack_into('>I', b, 0x804, 0x12)
struct.pack_into('>I', b, 0x810 + 0x20, 1)               # sound count
struct.pack_into('>I', b, 0x810 + 0x24, 0x900 - 0x810 - 0x24)   # record table offset
struct.pack_into('>I', b, 0x810 + 0x3c, 0x1000)          # data base
struct.pack_into('>IiiIII', b, 0x900, 0xabcd, 28, -1, (22050 << 16) | (0x13 << 8) | 1, 0, 0x40 + len(data))
ng = bytearray(0x40)
ng[0:4] = b'NGCA'
struct.pack_into('>II', ng, 4, len(data), 2)
for i in range(16):
    struct.pack_into('>h', ng, 0x0c + 2 * i, 0x100 * (i + 1))
b[0x1000:0x1040] = ng
b[0x1040:0x1040 + len(data)] = data
open(os.path.join(sys.argv[1], 't.000'), 'wb').write(b)
