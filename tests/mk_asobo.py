#!/usr/bin/env python3
"""Synthetic Asobo BigFile volume (see project/src/lib-asobo.h) with one stored
UserDefine_Z resource, one LZRS-compressed Bitmap_Z one DSP Sound_Z and one Mesh_Z triangle. usage: mk_asobo.py OUTDIR -> OUTDIR/TEST.DRV"""
import os, struct, sys

def h(s):
    T = []
    for i in range(256):
        c = i << 24
        for _ in range(8):
            c = ((c << 1) ^ 0x04C11DB7) & 0xffffffff if c & 0x80000000 else (c << 1) & 0xffffffff
        T.append(c)
    x = 0
    for ch in s.lower().encode():
        x = (x >> 8) ^ T[(ch ^ x) & 0xff]
    return x

def lzrs_literals(data):
    # Literal-only LZRS stream: flag words with the top bits clear.
    out = b''
    for i in range(0, len(data), 30):
        out += struct.pack('>I', 0) + data[i:i + 30]
    return out

B = lambda *a: struct.pack('>%dI' % len(a), *a)
text = b'user define data\n'
bmp_plain = struct.pack('>IIIBBBBBBH', 4, 4, 0, 14, 14, 3, 0, 0, 4, 0) + bytes(8)
comp = lzrs_literals(bmp_plain)
comp_body = struct.pack('<II', len(bmp_plain), len(comp) + 8) + comp
r1 = B(len(text) + 8, 8, len(text), 0, h('UserDefine_Z'), 0x1234) + b'\0' * 8 + text
r2 = B(len(comp_body) + 4, 4, len(bmp_plain), len(comp_body), h('Bitmap_Z'), 0x5678) + b'\0' * 4 + comp_body
# Sound_Z: 10-byte prefix + DSP header (28 samples, 32 nibbles, 22050 Hz) + 2 silent frames
dsp = struct.pack('>IIIHH', 28, 32, 22050, 0, 0) + struct.pack('>III', 0, 0, 2) + b''.join(struct.pack('>h', c) for c in [0x800] + [0] * 15)
dsp = dsp.ljust(0x60, b'\0')
snd = b'\0\0' + struct.pack('>I', 0x60 + 16) + b'\0\0\0\0' + dsp + bytes(16)
r3 = B(len(snd) + 8, 8, len(snd), 0, h('Sound_Z'), 0x9abc) + b'\0' * 8 + snd
# Mesh_Z (Wii layout): one triangle from a GX triangle-list display list
pos = b''.join(struct.pack('>3h', *v) for v in [(0, 0, 0), (4096, 0, 0), (0, 4096, 0)])
uvs = b''.join(struct.pack('>2h', *v) for v in [(0, 0), (1024, 0), (0, 1024)])
nrm = bytes([0, 0, 64] * 3)
dl = bytes([0x90]) + struct.pack('>H', 3) + b''.join(struct.pack('>3H', i, i, i) for i in range(3))
mesh = bytes(32) + B(1, 0xfeedf00d) + bytes(24) + bytes(20)
mesh += B(3) + pos + B(6) + uvs + B(9) + nrm + B(1, 32, len(dl)) + dl.ljust(32, b'\xcd') + B(1, 0x1)
r4 = B(len(mesh) + 8, 8, len(mesh), 0, h('Mesh_Z'), 0xdef0) + b'\0' * 8 + mesh
block = r1 + r2 + r3 + r4
padded = (len(block) + 2047) & ~2047
blockdesc = B(4, padded, len(block), 0, 0x1234, 0)
head = b'v1.06.63.01 - Asobo Studio - Internal Cross Technology'.ljust(0x100, b'\0')
head += B(0, 1, padded, padded, padded, 1, 0, 0) + blockdesc
out = head.ljust(0x800, b'\0') + block.ljust(padded, b'\0')
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'TEST.DRV'), 'wb').write(out)
