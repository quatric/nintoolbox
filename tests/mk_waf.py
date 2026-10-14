#!/usr/bin/env python3
# Synthetic Major Minor's Majestic March t.waf: two chained DSP chunks (data 16 / 8 bytes).
import struct, sys, os
def chunk(data, rate):
    nib = len(data) * 2
    h = bytearray(0x60)
    struct.pack_into('>IIIHHIII', h, 0, nib // 16 * 14, nib, rate, 0, 0, 2, nib - 1, 2)
    body = bytes(h) + data
    c = struct.pack('>HHIII', 1, 2, len(body), nib // 16 * 14, rate) + body
    return c + bytes(-len(c) % 32)
out = chunk(bytes(range(1, 17)), 24000) + chunk(bytes(range(0x21, 0x29)), 24000)
open(os.path.join(sys.argv[1], 't.waf'), 'wb').write(out)
