#!/usr/bin/env python3
# Synthetic Worms-style SPD/SPT audio bank: one DSP stream of two frames.
import struct, sys, os
out = sys.argv[1]
data = bytes([0x09, 1, 2, 3, 4, 5, 6, 7, 0x09, 8, 9, 10, 11, 12, 13, 14])
rec = struct.pack('>7I', 0, 44100, 0, 0, 31, 2, 0)
coef = struct.pack('>16h', *range(1, 17)) + struct.pack('>7H', 0, 9, 0, 0, 0, 0, 0)
open(os.path.join(out, 'b.spd'), 'wb').write(data)
open(os.path.join(out, 'b.spt'), 'wb').write(struct.pack('>I', 1) + rec + coef)
