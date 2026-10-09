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
# Wave bank (.WVD): two mono DSP waves, the second looped (sample count larger than the nibble count)
def wave_hdr(ns, nib, rate):
    h = bytearray(0x60)
    struct.pack_into('>III', h, 0, ns, nib, rate)
    struct.pack_into('>III', h, 0x10, 2, nib - 2, 2)
    struct.pack_into('>I', h, 0x1c, 0x030600d6)
    return h
w0 = wave_hdr(28, 32, 22050) + bytes([0x51]) * 16
w0 = bytes(w0) + bytes(0x80 - len(w0))
w1 = wave_hdr(42, 32, 18000) + bytes([0x62]) * 16 + bytes([0x63]) * 8
w1 = bytes(w1) + bytes(0x80 - len(w1))
wvd = bytearray(0x800)
wvd[0:4] = b'KNON'
wvd[8:12] = b'WII '
wvd[0x20:0x28] = b'KWBK' + struct.pack('>I', 8)
wi = 0x30
wsz = 8 + 2 * 0x60
wvd[wi:wi + 8] = b'WINF' + struct.pack('>I', wsz)
struct.pack_into('>HHI', wvd, wi + 8, 2, 0, 0x01020002)
for i, (rate, w) in enumerate(((22050, w0), (18000, w1))):
    struct.pack_into('>HHIII', wvd, wi + 0x10 + 0x60 * i, i, 0xb4, rate, 0, len(w))
wave = wi + 8 + wsz
body = b'WAVE' + struct.pack('>I', 8 + len(w0) + len(w1)) + struct.pack('>I', 0x01020002) + struct.pack('>I', 0) + w0 + w1
out = bytes(wvd[:wave]) + body
open(os.path.join(d, 't.wvd'), 'wb').write(out)
