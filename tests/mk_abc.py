#!/usr/bin/env python3
# Synthetic Sumo Digital t.abc: two DSP files (16 / 8 data bytes) with the 1..0x100 padding to a 0x100 boundary.
import struct, sys, os
def dsp(data, rate):
    nib = len(data) * 2
    h = bytearray(0x60)
    struct.pack_into('>IIIHHIII', h, 0, nib // 16 * 14, nib, rate, 0, 0, 2, nib - 1, 2)
    for i in range(16):
        struct.pack_into('>h', h, 0x1c + 2 * i, 0x100 * (i + 1))
    return bytes(h) + data
out = b''
for d, r in ((bytes(range(1, 17)), 44100), (bytes(range(0x21, 0x29)), 32000)):
    f = dsp(d, r)
    out += f + bytes((len(out) + len(f) + 0x100) // 0x100 * 0x100 - (len(out) + len(f)))
open(os.path.join(sys.argv[1], 't.abc'), 'wb').write(out)
