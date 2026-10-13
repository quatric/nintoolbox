#!/usr/bin/env python3
# Synthetic CRI ADX (standard, type 3): mono, 32000 Hz, 64 samples (2 frames), highpass 500 Hz.
# Frame 0 carries scale 0 (all samples 0); frame 1 has scale 99 and residual +1 for every sample, so with
# the filter active the output rises from 100 on the first sample.
import struct, sys, os
d = sys.argv[1]
hdr = bytearray(0x20)
hdr[0:2] = b'\x80\x00'
hdr[2:4] = struct.pack('>H', 0x1c)   # copyright at 0x1c -> data at 0x20
hdr[4:8] = bytes([3, 18, 4, 1])
hdr[8:16] = struct.pack('>II', 32000, 64)
hdr[0x10:0x14] = struct.pack('>HBB', 500, 4, 0)
hdr[0x1c:0x20] = b'(c)C'
f0 = struct.pack('>H', 0) + bytes(16)
f1 = struct.pack('>H', 99) + bytes([0x11] * 16)
open(os.path.join(d, 't.adx'), 'wb').write(bytes(hdr) + f0 + f1)
