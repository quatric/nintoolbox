#!/usr/bin/env python3
# Synthetic Ubisoft DARE sound package t.pk (Rabbids TV Party layout, 0x1f0008): one header BAO for a mono 22050 Hz
# DSP-ADPCM sound of 28 samples and the memory BAO holding its two ADPCM frames.
import struct, sys, os
hdr = bytearray(0xa8 + 0x40)
struct.pack_into('>I', hdr, 0, 0x011f0008)
struct.pack_into('<I', hdr, 4, 0x28)
struct.pack_into('>I', hdr, 0x20, 0x20000000)
h = 0x28
for off, val in ((0, 0x1001), (4, 1), (8, 16), (0x1c, 0x1002), (0x28, 0), (0x34, 0), (0x44, 1), (0x4c, 22050), (0x54, 28),
                 (0x5c, 0), (0x64, 9), (0x74, 0)):
    struct.pack_into('>I', hdr, h + off, val)
mini = 0xa8
for i in range(16):
    struct.pack_into('>h', hdr, mini + 0x10 + 2 * i, 0x100 * (i + 1))
struct.pack_into('>hh', hdr, mini + 0x34, 5, -3)
mem = bytearray(0x28) + bytes([0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88])
struct.pack_into('>I', mem, 0, 0x021f0008)
index = struct.pack('<IIII', 0x1001, len(hdr), 0x1002, len(mem))
head = bytearray(0x40)
head[0:4] = struct.pack('>I', 0x011f0008)
struct.pack_into('<II', head, 4, len(index), 0x40 + len(index) + len(hdr) + len(mem))
body = bytes(head) + index + bytes(hdr) + bytes(mem) + struct.pack('<II', 0, 0)
open(os.path.join(sys.argv[1], 't.pk'), 'wb').write(body)
