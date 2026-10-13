#!/usr/bin/env python3
# Synthetic Rebellion Asura Wii texture member (PDC World Championship Darts, type 1): 4x4 RGB5A3 as t.t1,
# top-left pixel red, every other pixel opaque green.
import struct, sys, os
d = sys.argv[1]
px = [0xfc00] + [0x83e0] * 15
f = struct.pack('>HHHH', 4, 4, 4, 4) + struct.pack('>IBBBB', 0, 5, 0, 0, 0) + bytes(16) + b''.join(struct.pack('>H', v) for v in px)
open(os.path.join(d, 't.t1'), 'wb').write(f)
