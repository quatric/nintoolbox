#!/usr/bin/env python3
# Synthetic Havok-style .hvt (" IVH") textures: 8x8 GX CMPR as t.hvt, 8x8 RGBA8 ("ARGB") as t2.hvt, 8x8 I8 ("GRY8") as t3.hvt.
import struct, sys, os
d = sys.argv[1]
blk = struct.pack('>HH', 0xffff, 0xffff) + bytes(4)
f = b' IVH' + struct.pack('>I', 2) + b'S3TW' + struct.pack('>III', 8, 8, 4) + blk * 4
open(os.path.join(d, 't.hvt'), 'wb').write(f)
f = b' IVH' + struct.pack('>I', 2) + b'ARGB' + struct.pack('>III', 8, 8, 32) + bytes(8 * 8 * 4)
open(os.path.join(d, 't2.hvt'), 'wb').write(f)
f = b' IVH' + struct.pack('>I', 2) + b'GRY8' + struct.pack('>III', 8, 8, 8) + bytes(8 * 8)
open(os.path.join(d, 't3.hvt'), 'wb').write(f)
