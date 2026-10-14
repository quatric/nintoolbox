#!/usr/bin/env python3
# Synthetic Stormfront t.seg (Wii, mono): 0x4000 header area, one 0x60-byte DSP header and 0x20 bytes of data.
import struct, sys, os
data = bytes(range(1, 0x21))
nib = len(data) * 2
b = bytearray(0x4000 + 0x2000)
b[0:8] = b'seg\0wii\0'
struct.pack_into('>IIIIII', b, 8, 2, len(b), len(data) + 0x60, 0, 44100, nib // 16 * 14)
struct.pack_into('>II', b, 0x20, 0, 1)
h = bytearray(0x60)
struct.pack_into('>IIIHHIII', h, 0, nib // 16 * 14, nib, 44100, 0, 0, 2, nib - 1, 2)
for i in range(16):
    struct.pack_into('>h', h, 0x1c + 2 * i, 0x100 * (i + 1))
b[0x4000:0x4060] = h
b[0x4060:0x4060 + len(data)] = data
open(os.path.join(sys.argv[1], 't.seg'), 'wb').write(b)
