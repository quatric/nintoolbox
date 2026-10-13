#!/usr/bin/env python3
# Synthetic Sonic TXAG texture archive with one inline-palette CI8 GVR (8x4, red / green indices)
import struct, sys, os
d = sys.argv[1]
w, h = 8, 4
pal = struct.pack('>256H', *([0xfc00, 0x83e0] + [0x8000] * 254))   # RGB5A3: red, green
idx = bytes([0] * 16 + [1] * 16)
body = pal + idx
gvr = b'GCIX' + struct.pack('>I', 8) + bytes(8) + b'GVRT' + struct.pack('<I', len(body) + 8) + bytes([0, 0, 0x28, 9]) \
    + struct.pack('>HH', w, h) + body
off = 8 + 0x28
ent = struct.pack('>II', off, len(gvr)) + b'RED_GREEN'.ljust(0x20, b'\0')
open(os.path.join(d, 't.txd'), 'wb').write(b'TXAG' + struct.pack('>I', 1) + ent + gvr)
