#!/usr/bin/env python3
# Synthetic Next Level Games wave bank (Mario Strikers Charged): t.resbun (container with a 0x03023703 wave table of
# two sounds, the second one cut short at the end of the file) plus the raw DSP-ADPCM data t.nlxwb.
import struct, sys, os
d = sys.argv[1]
data = bytes([0x33] + list(range(1, 8))) + bytes([0x44] + list(range(0x11, 0x18))) + bytes([0x55, 0xaa, 0xbb, 1, 2, 3])
# sound 0: frame 0 (nibbles 2..0xf), sound 1: frame 1 + a partial frame 2 (end nibble 0x2b = byte 0x15)
ents = [(22050, 0xf, 2), (44100, 0x2b, 0x12)]
tab = struct.pack('>I', len(ents))
for rate, end, start in ents:
    tab += struct.pack('>7I', 0, rate, 0, 0, end, start, 0)
for k in range(len(ents)):
    tab += b''.join(struct.pack('>h', 100 * k + i) for i in range(16)) + struct.pack('>7H', 0, 0x33, 0, 0, 0, 0, 0)
inner = struct.pack('>II', 0x03023703, len(tab)) + tab
res = struct.pack('>II', 0x80000001, len(inner)) + inner
open(os.path.join(d, 't.resbun'), 'wb').write(res)
open(os.path.join(d, 't.nlxwb'), 'wb').write(data)
