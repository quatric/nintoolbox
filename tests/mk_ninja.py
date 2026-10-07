#!/usr/bin/env python3
# Synthetic Ninja NJCM model: one triangle (type 0x23 vertices, one strip).
import struct, sys, os
out = sys.argv[1]
def w(*h): return b''.join(struct.pack('>H', x & 0xffff) for x in h)
# NJCM body
obj = struct.pack('>II3f3i3fII', 0, 0x34, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0)
model = struct.pack('>IIffff', 0x34 + 0x18, 0x34 + 0x18 + 8 + 48, 0, 0, 0, 1)
vl = struct.pack('>HHI', 0x8023, 14, 3)
for p in ((0, 0, 0), (1, 0, 0), (0, 1, 0)):
    vl += struct.pack('>3f', *p) + b'\xff\xff\xff\xff'
pl = w(0x2513, 4, 0xffff, 0xffff, 0xffff, 0xffff, 0x0, 0x0, 0x0, 0x0)
pl = w(0x3408, 0x4000, 0x8241, 11, 1, 3, 0, 0, 0, 1, 1024, 0, 2, 0, 1024) + w(0x00ff)
body = obj + model + vl + pl
body = body[:0x34] + struct.pack('>IIffff', 0x34 + 0x18, 0x34 + 0x18 + 8 + 48, 0, 0, 0, 1) + body[0x34 + 0x18:]
# NJTL
names = b'tex0\0'
tl = struct.pack('>II', 8, 1) + struct.pack('>III', 20, 0, 0) + names
open(os.path.join(out, 'm.dat'), 'wb').write(
    b'LTJN' + struct.pack('>I', len(tl)) + tl + b'MCJN' + struct.pack('>I', len(body)) + body)
