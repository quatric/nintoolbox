#!/usr/bin/env python3
"""Synthetic HVQM4 1.5 movie: 1 block, 1 video frame (ignored), 1 mono audio frame of 3 samples. usage: mk_h4m.py OUTDIR"""
import os, struct, sys
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
hdr = b'HVQM4 1.5'.ljust(16, b'\0') + struct.pack('>IIIIIIIIIHHBBBBBBBBI', 0x44, 0, 1, 1, 1, 33366, 0, 0, 8, 64, 48, 2, 2, 0, 0, 1, 4, 1, 1, 32000)
assert len(hdr) == 0x44
audio = struct.pack('>I', 3) + bytes([0x10, 0x00, 0x18])
frames = struct.pack('>HHI', 1, 0x10, 4) + b'\0\0\0\0' + struct.pack('>HHI', 0, 0, len(audio)) + audio
blk = struct.pack('>IIIII', 0, len(frames), 1, 1, 0x01000000) + frames
open(os.path.join(d, 't.h4m'), 'wb').write(hdr + blk)
