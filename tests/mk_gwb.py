#!/usr/bin/env python3
# Synthetic Ubisoft GWB+GWD (Monster 4x4: World Circuit) v7 bank: one mono and one stereo subsong as t.gwb/t.gwd.
import struct, sys, os
d = sys.argv[1]
il = 0x4000
mono = bytes([0x51]) * 16
st = il + 8                       # per channel: one full block and an 8-byte tail
stereo = bytes([0x62]) * il + bytes([0x73]) * il + bytes([0x64]) * 8 + bytes([0x75]) * 8
body = mono + stereo
def rec(rate, nib):
    r = bytearray(0x4a)
    struct.pack_into('>II', r, 0, 0, rate)
    struct.pack_into('>I', r, 0x10, nib)
    struct.pack_into('>I', r, 0x14, 2)
    struct.pack_into('>I', r, 0x1c + 0, 0x030600d6)
    return bytes(r)
g = bytearray()
g += bytes([7]) + struct.pack('>I', 0x42) + struct.pack('>I', 0) + struct.pack('>I', 2)
g += bytes([0x02]) + struct.pack('>I', 1) + struct.pack('>IIII', 2, 0, len(mono), 5) + rec(22050, 34)
g += bytes([0x0a]) + struct.pack('>I', 2) + struct.pack('>IIII', 2, len(mono), len(stereo), 5) + rec(32000, st * 2 + 2) + rec(32000, st * 2 + 2)
open(os.path.join(d, 't.gwb'), 'wb').write(bytes(g))
open(os.path.join(d, 't.gwd'), 'wb').write(body)
