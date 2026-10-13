#!/usr/bin/env python3
# Synthetic 4x4 DXT1 DDS (all pixels colour0 = pure red): t.dds
import struct, sys, os
d = sys.argv[1]
hdr = b'DDS ' + struct.pack('<I', 124) + struct.pack('<I', 0x1007) + struct.pack('<II', 4, 4) + struct.pack('<II', 8, 0) + struct.pack('<I', 1)
hdr += bytes(44) + struct.pack('<II4s', 32, 4, b'DXT1') + bytes(20) + struct.pack('<I', 0x1000) + bytes(16)
assert len(hdr) == 128, len(hdr)
open(os.path.join(d, 't.dds'), 'wb').write(hdr + struct.pack('<HHI', 0xf800, 0x0000, 0))
