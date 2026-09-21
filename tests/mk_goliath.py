#!/usr/bin/env python3
"""Synthetic Goliath engine "GS" package (see project/src/lib-goliath.h): a
big-endian chunk tree holding one named CMPR+I8 texture (format 4, non-square
so the height-before-width header order is exercised) and one named mono
DSP-ADPCM audio stream in a RIFX WAVE container.
usage: mk_goliath.py OUTDIR -> OUTDIR/T.pkz"""
import os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

TEX_W, TEX_H = 16, 8
LEVELS = 1


def chunk(cid, version, body, children=False):
    return struct.pack('>IHHII', cid, version, 1 if children else 0, 0, len(body)) + body


def name_record(name):
    # 92-byte record: hash, flags, then a 64-byte NUL-padded name field at 0x1c.
    body = bytearray(92)
    struct.pack_into('>II', body, 0, 0xdeadbeef, 4)
    nb = name.encode()
    body[0x1c:0x1c + len(nb)] = nb
    return chunk(0x8000138e, 5, bytes(body))


def texture():
    # CMPR: one 4x4 block per 8 bytes, 8x8 tiles -> 16x8 is two tiles.
    blocks = b''
    for i in range(TEX_W * TEX_H // 16):
        c0 = 0xf800 if i % 2 else 0x001f
        blocks += struct.pack('>HHI', c0, 0x0000, 0x00000000)
    # I8 alpha: 8x4 tiles, one byte per pixel.
    alpha = bytes((i * 7) & 0xff for i in range(TEX_W * TEX_H))

    head = bytearray(0x30)
    struct.pack_into('>IIII', head, 0, TEX_H, TEX_W, LEVELS, 4)
    struct.pack_into('>I', head, 0x10, len(blocks))
    desc = chunk(0x8000138d, 2,
        name_record('SynthTexture_C') + chunk(0x80000191, 7, chunk(0x80000197, 4, bytes(head)), True),
        True)
    return desc, blocks + alpha


def audio():
    frames, rate = 8, 22050
    samples = frames * 14
    adpcm = b''.join(bytes([0x17]) + bytes(((i * 13 + k * 29) & 0xff) for k in range(7))
        for i in range(frames))

    coefs = b''.join(struct.pack('>h', v) for v in
        (1785, -604, 3602, -1888, 3008, -1403, 3610, -1743,
         2425, -840, 3815, -1988, 3091, -1238, 3682, -1725))
    ext = struct.pack('>IHI', 0, 4, samples) + coefs + bytes(12)
    fmt = struct.pack('>HHIIHHH', 2, 1, rate, rate * 8 // 14, 32, 4, len(ext)) + ext
    wave = b'WAVE' + b'fmt ' + struct.pack('>I', len(fmt)) + fmt \
        + b'data' + struct.pack('>I', len(adpcm)) + adpcm
    rifx = b'RIFX' + struct.pack('>I', len(wave)) + wave

    desc = chunk(0x8000138d, 2, name_record('synth_voice_01') + chunk(0x80001133, 1, bytes(16)), True)
    return desc, rifx


tex_desc, tex_data = texture()
aud_desc, aud_data = audio()

body = chunk(0x80000011, 3, b'buildman - synthetic\0GS version 9.0.175008\0') \
    + chunk(0x80000026, 1, chunk(0x80000195, 2, tex_data) + chunk(0x80001134, 1, aud_data), True) \
    + tex_desc + aud_desc

with open(os.path.join(out, 'T.pkz'), 'wb') as f:
    f.write(chunk(0x80000001, 1, body, True))
