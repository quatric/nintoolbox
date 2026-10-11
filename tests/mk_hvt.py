#!/usr/bin/env python3
# Synthetic Havok-style .hvt (" IVH", S3TW) texture: 8x8 solid-white GX CMPR as t.hvt.
import struct, sys, os
d = sys.argv[1]
blk = struct.pack('>HH', 0xffff, 0xffff) + bytes(4)
f = b' IVH' + struct.pack('>I', 2) + b'S3TW' + struct.pack('>III', 8, 8, 4) + blk * 4
open(os.path.join(d, 't.hvt'), 'wb').write(f)
