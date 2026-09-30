#!/usr/bin/env python3
"""Synthetic Eden Games ".wsi" blocked stereo DSP-ADPCM stream (see
project/src/lib-wsi.h): three block sets of 0x110 bytes per channel, the first
of each channel carrying its own 0x60-byte DSP header.
usage: mk_wsi.py OUTDIR -> OUTDIR/T.wsi"""
import os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
BS, SETS, CH = 0x110, 3, 2
frames = [(BS - 0x10 - 0x60) // 8] + [(BS - 0x10) // 8] * (SETS - 1)
total_frames = sum(frames)
samples, nibbles = total_frames * 14, total_frames * 16


def dsp_header(ch):
    h = bytearray(0x60)
    struct.pack_into('>III', h, 0, samples, nibbles, 32000)
    struct.pack_into('>H', h, 0x0c, 0)
    struct.pack_into('>16h', h, 0x1c, *[(ch + 1) * 100 + i for i in range(16)])
    return bytes(h)


data = struct.pack('>II', 0x20, CH).ljust(0x20, b'\0')
for s in range(SETS):
    for c in range(CH):
        blk = struct.pack('>IIII', BS, 1, c + 1, 0)
        if s == 0:
            blk += dsp_header(c)
        blk += bytes((0x10 * 0 + ((s * 7 + c * 3 + i) & 0x7f)) for i in range(frames[s] * 8))
        assert len(blk) == BS
        data += blk
open(os.path.join(out, 'T.wsi'), 'wb').write(data)
print(samples)
