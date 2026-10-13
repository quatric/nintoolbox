#!/usr/bin/env python3
# Synthetic Sega GTS texture set (Ghost Squad): two 8x8 RGB565 GVR textures named "aaa" (green) and "bbb".
import struct, sys, os
d = sys.argv[1]
w = h = 8
gvr = b'GCIX' + bytes(12) + b'GVRT' + bytes(6) + struct.pack('>BBHH', 0, 4, w, h) + struct.pack('>H', 0x07E0) * (w * h)
def entry(name):
    e = struct.pack('>I', len(gvr)) + name.ljust(0x3c, b'\0') + gvr
    return e + bytes(-len(e) % 32)
e0, e1 = entry(b'aaa'), entry(b'bbb')
off0 = 0x40
off1 = off0 + len(e0)
hdr = struct.pack('>I', off1 + len(e1)) + b'gts1' + struct.pack('>HH', 2, 0) + struct.pack('>II', off0, off1)
hdr += b'\xff' * (0x40 - len(hdr))
open(os.path.join(d, 't.gts'), 'wb').write(hdr + e0 + e1)
