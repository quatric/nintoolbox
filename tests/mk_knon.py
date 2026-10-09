#!/usr/bin/env python3
# Synthetic Paon KNON streams (Donkey Kong Barrel Blast): t.str (PCM16BE) and t.asr (DSP, KAST).
import struct, sys, os
d = sys.argv[1]
def base(tag, nbytes, rate):
    f = bytearray(0x800)
    f[0:4] = b'KNON'
    f[8:12] = b'WII '
    f[0x20:0x24] = tag
    struct.pack_into('>II', f, 0x3c, nbytes, rate)
    return f
# PCM: 2 blocks of 8 frames; left = 1..8, right = 101..108 in each 0x10 block pair
pcm = base(b'KPST', 0x40, 32000)
for blk in range(2):
    for k in range(8):
        pcm += struct.pack('>h', blk * 8 + k + 1)
    for k in range(8):
        pcm += struct.pack('>h', blk * 8 + k + 101)
open(os.path.join(d, 't.str'), 'wb').write(bytes(pcm))
# DSP: two channels, 0x10 interleave, headers at 0x70 and 0xd0 (current address / predictor unset)
frames = 4
nib = frames * 16
dsp = base(b'KAST', frames * 8 * 2, 32000)
for c in range(2):
    h = bytearray(0x60)
    struct.pack_into('>III', h, 0, frames * 14, nib, 32000)
    struct.pack_into('>I', h, 0x1c, 0x030600d6)
    dsp[0x70 + 0x60 * c:0x70 + 0x60 * c + 0x60] = h
for blk in range(frames // 2):
    dsp += bytes([0x31 + blk]) * 8 + bytes([0x11]) * 8
    dsp += bytes([0x41 + blk]) * 8 + bytes([0x22]) * 8
open(os.path.join(d, 't.asr'), 'wb').write(bytes(dsp))
