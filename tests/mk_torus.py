#!/usr/bin/env python3
"""Synthetic Torus Games hunkfile (see project/src/lib-torus.h): one 8x8 CMPR
texture and one mono DSP-ADPCM stream whose raw file lives in ../SOUND.
usage: mk_torus.py OUTDIR -> OUTDIR/HUNKFILES/T.hnk, OUTDIR/SOUND/aabbccdd.raw"""
import os, struct, sys
out = sys.argv[1]
os.makedirs(out + '/HUNKFILES', exist_ok=True); os.makedirs(out + '/SOUND', exist_ok=True)

def chunk(kind, payload):
    return struct.pack('<IHH', len(payload), kind, 4) + payload

def name(cls, nm, nchunks):
    a, b = cls.encode() + b'\0', nm.encode() + b'\0'
    return chunk(0x71, struct.pack('<5H', 1, 13, nchunks, len(a), len(b)) + a + b)

hdr = bytearray(0x5c)
struct.pack_into('>HH', hdr, 12, 8, 8); hdr[26] = 1
pix = struct.pack('>HHI', 0xf800, 0x001f, 0x1b1b1b1b) * 4      # 4 CMPR sub-blocks
snd = bytearray(110); snd[0:4] = b'IWAR'; snd[6] = 1
struct.pack_into('>II', snd, 8, 28, 32000)
struct.pack_into('>16h', snd, 64, *([0x800, 0] * 8))
nm = b'\x40\0\0\0' + b'aabbccdd.raw\0'
d = chunk(0x70, bytes(0x250))
d += name('TSETexture', 'Tex_d', 2) + chunk(0x150, bytes(hdr)) + chunk(0x151, pix) + chunk(0x72, b'')
d += name('SqueakStream', 'Voice_1A', 3) + chunk(0x92, bytes(snd)) + chunk(0x93, nm) + chunk(0x72, b'')
open(out + '/HUNKFILES/T.hnk', 'wb').write(d)
open(out + '/SOUND/aabbccdd.raw', 'wb').write(bytes([0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77] * 2))
