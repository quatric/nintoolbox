#!/usr/bin/env python3
# Synthetic Need for Speed: Hot Pursuit t.stm: two records (a model and a texture-like blob) over 0x40 / 0x20 bytes.
import struct, sys, os
recs = [(b'car_a.mod', 0x100, 0x40), (b'sign.tex', 0x140, 0x20)]
b = bytearray(0x10 + 0x40 * len(recs))
b[0:12] = b'STME\x01\x01\x01\x0fWI01'
struct.pack_into('>I', b, 0xc, len(recs))
for i, (n, off, sz) in enumerate(recs):
    o = 0x10 + 0x40 * i
    struct.pack_into('>I', b, o, 4)
    b[o + 4:o + 4 + len(n)] = n
    struct.pack_into('>III', b, o + 0x28, off, sz, 0)
b = b.ljust(0x100, b'\0') + bytes(range(0x40)) + bytes(range(0x20))
open(os.path.join(sys.argv[1], 't.stm'), 'wb').write(b)
