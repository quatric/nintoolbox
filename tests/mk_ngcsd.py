#!/usr/bin/env python3
# Synthetic "_NGCSD02" stereo stream: one DSP header per channel from 0x18, 8-byte frames interleaved.
import struct, sys, os
out = sys.argv[1]
def hdr(ps):
    h = bytearray(0x60)
    struct.pack_into('>III', h, 0, 28, 32, 32000)       # 2 frames of 14 samples
    struct.pack_into('>III', h, 0x10, 2, 31, 2)
    for i in range(16): struct.pack_into('>h', h, 0x1c + 2 * i, i)
    h[0x3f] = ps
    return bytes(h)
l = bytes([0x10, 1, 2, 3, 4, 5, 6, 7, 0x20, 8, 9, 10, 11, 12, 13, 14])
r = bytes([0x30, 21, 22, 23, 24, 25, 26, 27, 0x40, 28, 29, 30, 31, 32, 33, 34])
data = b''.join(l[i:i+8] + r[i:i+8] for i in (0, 8))
raw = b'_NGCSD02' + struct.pack('>f', 0.001) + struct.pack('>I', 28) + bytes([0, 0, 2, 0, 0, 0, 0, 0]) + hdr(0x10) + hdr(0x30) + data + b'\xaa' * 24
open(os.path.join(out, 't.ngcsd'), 'wb').write(raw)
