#!/usr/bin/env python3
# Synthetic Ubisoft Red Steel t.sb7 (Wii, version 0x00180006): one internal mono DSP sound of 32 data bytes (56 samples).
import struct, sys, os
data = bytes(range(1, 0x21))
o2 = 0x1c
ox = o2 + 0x6c
xs = 0x40
o3 = ox + xs
so = o3 + 8
b = bytearray(so + len(data))
struct.pack_into('>7I', b, 0, 0x00180006, 0, 1, 1, xs, 0, 0)
e = o2
struct.pack_into('>III', b, e, 0x1234, 1, len(data))     # id, type 1, stream size
struct.pack_into('>II', b, e + 0x0c, 0, 0)               # extra offset, stream offset
struct.pack_into('>I', b, e + 0x3c, 56)                  # samples
struct.pack_into('>I', b, e + 0x50, 22050)               # rate
struct.pack_into('>II', b, e + 0x58, 1, 0)               # channels, stream type 0 = hardware DSP
for i in range(16):
    struct.pack_into('>h', b, ox + 0x10 + 2 * i, 0x100 * (i + 1))
struct.pack_into('>II', b, o3, 0, len(data))             # subblock 0
b[so:] = data
open(os.path.join(sys.argv[1], 't.sb7'), 'wb').write(b)
