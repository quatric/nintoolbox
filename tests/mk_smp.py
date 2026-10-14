#!/usr/bin/env python3
# Synthetic Guilty Party t.smp: version 8, 0x80-byte header, mono DSP with 0x20 data bytes (56 samples).
import struct, sys, os
data = bytes(range(1, 0x21))
b = bytearray(0x80)
struct.pack_into('<I', b, 0, 8)
struct.pack_into('<IIII', b, 0x14, 0, 56, 0x80, len(data))
struct.pack_into('<IBBH', b, 0x24, 2, 1, 4, 32000)
for i in range(16):
    struct.pack_into('<h', b, 0x2c + 2 * i, 0x100 * (i + 1))
open(os.path.join(sys.argv[1], 't.smp'), 'wb').write(bytes(b) + data)
