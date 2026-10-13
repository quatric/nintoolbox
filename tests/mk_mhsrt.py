#!/usr/bin/env python3
# Synthetic Monster Hunter Tri sound table + stream: t.srt (one stereo DSP entry, 32000 Hz, predictor 0)
# and stream/t.ssd with one ADPCM frame per channel in 0x10000-byte interleaved blocks (ch0 samples 7,
# ch1 samples 2; the zero padding after is trimmed).
import struct, sys, os
d = sys.argv[1]
os.makedirs(os.path.join(d, 'stream'), exist_ok=True)
coef = struct.pack('>16h', *([0] * 16))
entry = bytearray(0x90)
entry[8:12] = struct.pack('>I', 32000)
entry[12] = 1
for c in range(2):
    o = 0x18 + 0x40 * c
    entry[o:o + 8] = struct.pack('>II', 0x1000, 2 + (0x20000 if c else 0))
    entry[o + 8:o + 40] = coef
dsp = b'DSP\0' + struct.pack('>III', 1, 0x10, 0) + struct.pack('>I', 0x10) + bytes(12) + bytes(entry)
srt = bytearray(0x40) + dsp
srt[0:8] = struct.pack('>II', 0x40, 0)
open(os.path.join(d, 'T_Demo_01.srt'), 'wb').write(bytes(srt))
ssd = bytearray(0x20000)
ssd[0:8] = bytes([0x00, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77])
ssd[0x10000:0x10008] = bytes([0x00, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22])
open(os.path.join(d, 'stream', 'demo_01_t.ssd'), 'wb').write(bytes(ssd))
