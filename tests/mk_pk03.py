#!/usr/bin/env python3
# Synthetic PK03 package t.pak: one LZ11 member (literal-only stream) and one stored member.
import struct, sys, os
def lz11_lit(d):
    out = bytearray(b'\x11' + len(d).to_bytes(3, 'little'))
    for i in range(0, len(d), 8):
        out.append(0); out += d[i:i + 8]
    return bytes(out)
a = b'pk03 compressed member'
za = lz11_lit(a)
b = b'pk03 stored'
base = 12 + 24
oa = (base + 15) & ~15
ob = (oa + len(za) + 15) & ~15
d = b'PK03' + struct.pack('>II', 0, 2) + struct.pack('>III', 0x11, oa, (len(za) << 8) | 1) + struct.pack('>III', 0x22, ob, len(b) << 8)
d = d.ljust(oa, b'\0') + za
d = d.ljust(ob, b'\0') + b
open(os.path.join(sys.argv[1], 't.pak'), 'wb').write(d)
